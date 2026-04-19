#include "risk_factor_adjustable_model.h"

#include "events/sync_message.h"
#include "hgps_core/diagnostics/internal_error.h"

#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace {

using RiskFactorAdjustmentMessage = hgps::SyncDataMessage<hgps::RiskFactorSexAgeTable>;

struct FirstMoment {
  public:
    void append(double value) noexcept {
        ++count_;
        sum_ += value;
    }

    double mean() const noexcept {
        if (count_ == 0) {
            return std::nan("");
        }

        return sum_ / count_;
    }

  private:
    int count_{};
    double sum_{};
};

std::optional<double> get_factor_value_for_person(const hgps::Person &person,
                                                  const hgps::core::Identifier &factor) {
    const std::string &key = factor.to_string();

    if (key == "income" || key == "Income") {
        if (person.risk_factors.contains(factor)) {
            return person.risk_factors.at(factor);
        }
        return std::nullopt;
    }

    if (key == "PhysicalActivity") {
        return person.physical_activity;
    }

    if (person.risk_factors.contains(factor)) {
        return person.risk_factors.at(factor);
    }

    return std::nullopt;
}

bool should_include_in_simulated_mean(
    const double value, const hgps::core::Identifier &factor,
    const std::unordered_set<hgps::core::Identifier> &logistic_factors) {
    if (!logistic_factors.contains(factor)) {
        return true;
    }

    return value != 0.0;
}

} // namespace

namespace hgps {

RiskFactorAdjustableModel::RiskFactorAdjustableModel(
    std::shared_ptr<RiskFactorSexAgeTable> expected,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
    std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps, const TrendType trend_type,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend,
    std::shared_ptr<std::unordered_map<core::Identifier, double>>
        expected_income_trend_decay_factors)
    : expected_{std::move(expected)}, expected_trend_{std::move(expected_trend)},
      trend_steps_{std::move(trend_steps)}, trend_type_{trend_type},
      expected_income_trend_{std::move(expected_income_trend)},
      expected_income_trend_decay_factors_{std::move(expected_income_trend_decay_factors)} {}

double RiskFactorAdjustableModel::get_expected(RuntimeContext &context, const core::Gender sex,
                                               const int age, const core::Identifier &factor,
                                               OptionalRange range, const bool apply_trend) const {
    if (!expected_ || !expected_->contains(sex, factor)) {
        throw core::InternalError(fmt::format(
            "Expected value not found for factor '{}' (sex={}) in factors mean table.",
            factor.to_string(), sex == core::Gender::male ? "male" : "female"));
    }

    double expected = expected_->at(sex, factor).at(age);

    if (apply_trend && trend_type_ != TrendType::Null) {
        const int elapsed_time = context.time_now() - context.start_time();

        switch (trend_type_) {
        case TrendType::Null:
            break;

        case TrendType::UPFTrend: {
            if (expected_trend_) {
                const int t = std::min(elapsed_time, get_trend_steps(factor));
                expected *= std::pow(expected_trend_->at(factor), t);
            }
            break;
        }

        case TrendType::IncomeTrend: {
            if (elapsed_time > 0 && expected_income_trend_ && expected_income_trend_decay_factors_) {
                const double decay_factor = expected_income_trend_decay_factors_->at(factor);
                const double expected_income_trend = expected_income_trend_->at(factor);
                expected *= expected_income_trend * std::exp(decay_factor * elapsed_time);
            }
            break;
        }
        }
    }

    if (range.has_value()) {
        expected = range.value().get().clamp(expected);
    }

    return expected;
}

void RiskFactorAdjustableModel::adjust_risk_factors(
    RuntimeContext &context, const std::vector<core::Identifier> &factors, OptionalRanges ranges,
    const bool apply_trend) const {
    RiskFactorSexAgeTable adjustments;

    if (context.scenario().type() == ScenarioType::baseline) {
        adjustments = calculate_adjustments(context, factors, ranges, apply_trend);
    } else {
        auto message = context.sync_channel().try_receive(context.sync_timeout_millis());
        if (!message.has_value()) {
            throw core::InternalError(
                "Simulation out of sync, receive baseline adjustments message has timed out");
        }

        auto &base_ptr = message.value();
        auto *message_ptr = dynamic_cast<RiskFactorAdjustmentMessage *>(base_ptr.get());
        if (!message_ptr) {
            throw core::InternalError(
                "Simulation out of sync, failed to receive a baseline adjustments message");
        }

        adjustments = message_ptr->data();
    }

    for (auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }

        for (std::size_t i = 0; i < factors.size(); ++i) {
            const core::Identifier &factor = factors[i];
            const double delta = adjustments.at(person.gender, factor).at(person.age);

            const std::string &factor_name = factor.to_string();
            std::string factor_name_lower = factor_name;
            std::ranges::transform(factor_name_lower, factor_name_lower.begin(), ::tolower);

            if (factor_name_lower == "income") {
                double current_value = 0.0;
                if (person.risk_factors.contains(factor)) {
                    current_value = person.risk_factors.at(factor);
                } else if (person.income_continuous > 0.0) {
                    current_value = person.income_continuous;
                }

                const double adjusted_value = current_value + delta;
                person.risk_factors[factor] = adjusted_value;
                if (person.income_continuous > 0.0) {
                    person.income_continuous = adjusted_value;
                }
            } else if (factor_name == "PhysicalActivity") {
                double current_value = person.physical_activity;
                const core::Identifier physical_activity_id("PhysicalActivity");
                if (person.risk_factors.contains(physical_activity_id)) {
                    current_value = person.risk_factors.at(physical_activity_id);
                }

                double adjusted_value = current_value + delta;
                if (ranges.has_value() && i < ranges.value().get().size()) {
                    const auto &range = ranges.value().get()[i];
                    adjusted_value = range.clamp(adjusted_value);
                }

                person.physical_activity = adjusted_value;
                person.risk_factors[physical_activity_id] = adjusted_value;
            } else {
                double value = person.risk_factors.at(factor) + delta;
                if (ranges.has_value() && i < ranges.value().get().size()) {
                    const auto &range = ranges.value().get()[i];
                    value = range.clamp(value);
                }

                person.risk_factors.at(factor) = value;
            }
        }
    }

    if (context.scenario().type() == ScenarioType::baseline) {
        context.sync_channel().send(std::make_unique<RiskFactorAdjustmentMessage>(
            context.current_run(), context.time_now(), std::move(adjustments)));
    }
}

int RiskFactorAdjustableModel::get_trend_steps(const core::Identifier &factor) const {
    if (trend_steps_) {
        return trend_steps_->at(factor);
    }

    return 0;
}

void RiskFactorAdjustableModel::set_logistic_factors(
    const std::unordered_set<core::Identifier> &logistic_factors) {
    logistic_factors_ = logistic_factors;
}

RiskFactorSexAgeTable
RiskFactorAdjustableModel::calculate_adjustments(RuntimeContext &context,
                                                 const std::vector<core::Identifier> &factors,
                                                 OptionalRanges ranges,
                                                 const bool apply_trend) const {
    const auto age_range = context.inputs().settings().age_range();
    const auto age_count = age_range.upper() + 1;

    const auto simulated_means =
        calculate_simulated_mean(context.population(), age_range, factors, logistic_factors_);

    auto adjustments = RiskFactorSexAgeTable{};
    for (const auto &[sex, simulated_means_by_sex] : simulated_means) {
        for (std::size_t i = 0; i < factors.size(); ++i) {
            const core::Identifier &factor = factors[i];

            if (!expected_->contains(sex, factor)) {
                continue;
            }

            OptionalRange range;
            if (ranges.has_value()) {
                range = OptionalRange{ranges.value().get().at(i)};
            }

            adjustments.emplace(sex, factor, std::vector<double>(age_count));
            for (auto age = age_range.lower(); age <= age_range.upper(); ++age) {
                const double expect = get_expected(context, sex, age, factor, range, apply_trend);
                const double sim_mean = simulated_means_by_sex.at(factor).at(age);

                double delta = 0.0;
                if (!std::isnan(sim_mean)) {
                    delta = expect - sim_mean;
                }

                adjustments.at(sex, factor).at(age) = delta;
            }
        }
    }

    return adjustments;
}

RiskFactorSexAgeTable RiskFactorAdjustableModel::calculate_simulated_mean(
    Population &population, const core::IntegerInterval age_range,
    const std::vector<core::Identifier> &factors,
    const std::unordered_set<core::Identifier> &logistic_factors) {
    const auto age_count = age_range.upper() + 1;

    auto moments = UnorderedMap2d<core::Gender, core::Identifier, std::vector<FirstMoment>>{};

    for (const auto &person : population) {
        if (!person.is_active()) {
            continue;
        }

        for (const auto &factor : factors) {
            if (!moments.contains(person.gender, factor)) {
                moments.emplace(person.gender, factor, std::vector<FirstMoment>(age_count));
            }

            const auto opt_value = get_factor_value_for_person(person, factor);
            if (!opt_value.has_value()) {
                continue;
            }

            const double value = *opt_value;
            if (!should_include_in_simulated_mean(value, factor, logistic_factors)) {
                continue;
            }

            moments.at(person.gender, factor).at(person.age).append(value);
        }
    }

    auto means = RiskFactorSexAgeTable{};
    for (const auto &[sex, moments_by_sex] : moments) {
        for (const auto &factor : factors) {
            means.emplace(sex, factor, std::vector<double>(age_count));
            for (auto age = age_range.lower(); age <= age_range.upper(); ++age) {
                means.at(sex, factor).at(age) = moments_by_sex.at(factor).at(age).mean();
            }
        }
    }

    return means;
}

RiskFactorAdjustableModelDefinition::RiskFactorAdjustableModelDefinition(
    std::unique_ptr<RiskFactorSexAgeTable> expected,
    std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
    std::unique_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
    const TrendType trend_type)
    : expected_{std::move(expected)}, expected_trend_{std::move(expected_trend)},
      trend_steps_{std::move(trend_steps)}, trend_type_{trend_type} {
    if (!expected_ || expected_->empty()) {
        throw core::InternalError("Risk factor expected value mapping is empty");
    }

    if (trend_type_ != TrendType::Null) {
        if (!expected_trend_ || expected_trend_->empty()) {
            throw core::InternalError("Risk factor expected trend mapping is empty");
        }
        if (!trend_steps_ || trend_steps_->empty()) {
            throw core::InternalError("Risk factor trend steps mapping is empty");
        }
    }
}

} // namespace hgps