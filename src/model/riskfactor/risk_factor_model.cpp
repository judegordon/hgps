#include "risk_factor_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {
namespace {

/// A running mean in constant space. An empty mean is NaN, which is how "nobody of this age and
/// sex" reaches the adjustment as "do not adjust".
class FirstMoment {
  public:
    void append(double value) noexcept {
        ++count_;
        sum_ += value;
    }

    double mean() const noexcept {
        return count_ == 0 ? std::numeric_limits<double>::quiet_NaN()
                           : sum_ / static_cast<double>(count_);
    }

  private:
    int count_{};
    double sum_{};
};

const core::Identifier kPhysicalActivity{"physicalactivity"};
const core::Identifier kIncome{"income"};

/// The value of a factor for the simulated mean, or nullopt if this person has no value for it.
///
/// Income and physical activity live in their own members as well as in the factor map, because
/// the analysis output and the disease models read them from there.
std::optional<double> factor_value(const Person &person, const core::Identifier &factor) {
    if (factor == kPhysicalActivity) {
        const auto found = person.risk_factors.find(factor);
        return found != person.risk_factors.end() ? found->second : person.physical_activity;
    }

    const auto found = person.risk_factors.find(factor);
    if (found != person.risk_factors.end()) {
        return found->second;
    }

    if (factor == kIncome && person.income_continuous > 0.0) {
        return person.income_continuous;
    }

    return std::nullopt;
}

} // namespace

AdjustableRiskFactorModel::AdjustableRiskFactorModel(
    std::shared_ptr<const SexAgeFactorTable> expected,
    std::shared_ptr<const std::map<core::Identifier, double>> trend,
    std::shared_ptr<const std::map<core::Identifier, int>> trend_steps, TrendType trend_type,
    std::shared_ptr<const std::map<core::Identifier, double>> decay)
    : expected_{std::move(expected)}, trend_{std::move(trend)},
      trend_steps_{std::move(trend_steps)}, decay_{std::move(decay)}, trend_type_{trend_type} {
    if (!expected_ || expected_->empty()) {
        throw diag::InternalError("the risk factor expected-value table is empty");
    }

    if (trend_type_ != TrendType::Null && (!trend_ || trend_->empty())) {
        throw diag::InternalError("a trend is configured but the trend table is empty");
    }
    if (trend_type_ == TrendType::UpfTrend && (!trend_steps_ || trend_steps_->empty())) {
        throw diag::InternalError(
            "the UPF trend is configured but the trend steps table is empty");
    }
    if (trend_type_ == TrendType::IncomeTrend && (!decay_ || decay_->empty())) {
        throw diag::InternalError(
            "the income trend is configured but the decay factor table is empty");
    }
}

int AdjustableRiskFactorModel::get_trend_steps(const core::Identifier &factor) const {
    if (!trend_steps_) {
        return 0;
    }

    const auto found = trend_steps_->find(factor);
    return found == trend_steps_->end() ? 0 : found->second;
}

void AdjustableRiskFactorModel::set_logistic_factors(std::vector<core::Identifier> factors) {
    logistic_factors_ = std::move(factors);
}

double AdjustableRiskFactorModel::get_expected(RuntimeContext &context, core::Gender sex, int age,
                                                const core::Identifier &factor,
                                                std::optional<core::DoubleInterval> range,
                                                bool apply_trend) const {
    return expected_from(*expected_, context, sex, age, factor, range, apply_trend);
}

double AdjustableRiskFactorModel::expected_from(const SexAgeFactorTable &table,
                                                 RuntimeContext &context, core::Gender sex,
                                                 int age, const core::Identifier &factor,
                                                 std::optional<core::DoubleInterval> range,
                                                 bool apply_trend) const {
    if (!table.contains(sex, factor)) {
        throw diag::InternalError(fmt::format(
            "no expected value for factor '{}' ({}) in the FactorsMean table; the model names a "
            "factor its own calibration data does not have a column for",
            factor.to_string(), sex == core::Gender::male ? "male" : "female"));
    }

    const auto &by_age = table.at(sex, factor);
    if (age < 0 || static_cast<std::size_t>(age) >= by_age.size()) {
        throw diag::InternalError(
            fmt::format("the FactorsMean table for '{}' has no row for age {}",
                        factor.to_string(), age));
    }

    double expected = by_age.at(static_cast<std::size_t>(age));

    if (apply_trend && trend_type_ != TrendType::Null) {
        const int elapsed = context.time_now() - context.start_time();

        switch (trend_type_) {
        case TrendType::Null:
            break;
        case TrendType::UpfTrend:
            if (trend_ && trend_->contains(factor)) {
                const int steps = std::min(elapsed, get_trend_steps(factor));
                expected *= std::pow(trend_->at(factor), steps);
            }
            break;
        case TrendType::IncomeTrend:
            // From the second year only: at the start year the expected value is as measured.
            // The trend does not stop after a number of steps as the UPF one does; it decays.
            if (elapsed > 0 && trend_ && trend_->contains(factor) && decay_ &&
                decay_->contains(factor)) {
                expected *= trend_->at(factor) * std::exp(decay_->at(factor) * elapsed);
            }
            break;
        }
    }

    if (range.has_value()) {
        expected = range->clamp(expected);
    }

    return expected;
}

SexAgeFactorTable AdjustableRiskFactorModel::calculate_simulated_mean(
    const Population &population, core::IntegerInterval age_range,
    const std::vector<core::Identifier> &factors,
    const std::vector<core::Identifier> &logistic_factors,
    std::optional<std::size_t> income_stratum) {
    const auto age_count = static_cast<std::size_t>(age_range.upper()) + 1;

    Map2d<core::Gender, core::Identifier, std::vector<FirstMoment>> moments;
    for (const auto sex : {core::Gender::male, core::Gender::female}) {
        for (const auto &factor : factors) {
            moments.emplace(sex, factor, std::vector<FirstMoment>(age_count));
        }
    }

    // In slot order, serial: a per-person mean is not worth a parallel region, and the order is
    // then plainly the population's.
    for (const auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        if (static_cast<std::size_t>(person.age) >= age_count) {
            continue;
        }
        if (income_stratum.has_value() &&
            (!person.has_income_adjustment_stratum ||
             person.income_adjustment_stratum != *income_stratum)) {
            continue;
        }

        for (const auto &factor : factors) {
            const auto value = factor_value(person, factor);
            if (!value.has_value()) {
                continue;
            }

            // A two-stage factor's zeros mean "not applicable", so they would drag the mean down
            // and the adjustment would chase a number nobody has.
            if (*value == 0.0 && std::find(logistic_factors.begin(), logistic_factors.end(),
                                           factor) != logistic_factors.end()) {
                continue;
            }

            moments.at(person.gender, factor).at(static_cast<std::size_t>(person.age)).append(
                *value);
        }
    }

    SexAgeFactorTable means;
    for (const auto sex : {core::Gender::male, core::Gender::female}) {
        for (const auto &factor : factors) {
            std::vector<double> by_age(age_count, std::numeric_limits<double>::quiet_NaN());
            for (std::size_t age = 0; age < age_count; ++age) {
                by_age[age] = moments.at(sex, factor).at(age).mean();
            }
            means.emplace(sex, factor, std::move(by_age));
        }
    }

    return means;
}

sim::AdjustmentTable AdjustableRiskFactorModel::calculate_adjustments(
    RuntimeContext &context, const std::vector<core::Identifier> &factors,
    const std::vector<core::DoubleInterval> *ranges, bool apply_trend,
    const AdjustmentScope &scope) const {
    const auto age_range = context.age_range();
    const auto age_count = static_cast<std::size_t>(age_range.upper()) + 1;

    const auto *table = scope.expected_table != nullptr ? scope.expected_table : expected_.get();

    const auto simulated = calculate_simulated_mean(context.population(), age_range, factors,
                                                    logistic_factors_, scope.income_stratum);

    sim::AdjustmentTable adjustments;
    for (const auto sex : {core::Gender::male, core::Gender::female}) {
        for (std::size_t i = 0; i < factors.size(); ++i) {
            const auto &factor = factors[i];
            if (!table->contains(sex, factor)) {
                // A factor with no calibration column is left alone. The baseline prints a warning
                // to stdout and carries on; this is the same behaviour, reported through the
                // metrics so it appears in the results file rather than in a scrollback.
                context.metrics()[fmt::format("UncalibratedFactor.{}", factor.to_string())] = 1.0;
                continue;
            }

            std::optional<core::DoubleInterval> range;
            if (ranges != nullptr && i < ranges->size()) {
                range = (*ranges)[i];
            }

            // Physical activity's *target* is never clamped, even though its adjusted values are.
            // The two clamps are different things: clamping what a person ends up with keeps them
            // inside the factor's domain, while clamping the expected value aims the calibration
            // at a number the data does not say. It matters here because the FINCH FactorsMean
            // table legitimately puts physical activity at 1.2 for a newborn, below the
            // configured lower bound of 1.4 — clamping the target to 1.4 leaves the newborn band
            // a tenth of a unit high and, because the shifted values then clear the bound instead
            // of piling up on it, noticeably wider. This is the same ruling the risk factors got
            // in the previous run: the adjustment's target is the table's value.
            if (factor == kPhysicalActivity) {
                range.reset();
            }

            std::vector<double> deltas(age_count, 0.0);
            for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
                // The table, not the virtual get_expected. They differ for exactly one factor:
                // the Kevin Hall model *derives* an adult's expected weight from a regression on
                // their expected energy intake, height, age and activity level, and that
                // regression is a fit **to** the table's own Weight column — 86.1912 against the
                // table's 86.1864 for a 40-year-old man in the FINCH pack. Calibrating the
                // population onto the fit rather than onto the measurement it approximates would
                // be calibrating to the wrong number, by five grams per person. The derived value
                // is what a person's weight is *generated* from; the measurement is what the
                // population's mean is calibrated *to*.
                const double expected =
                    expected_from(*table, context, sex, age, factor, range, apply_trend);
                const double mean = simulated.at(sex, factor).at(static_cast<std::size_t>(age));

                // A NaN mean means nobody of this age and sex has the factor, so there is nothing
                // to calibrate and the delta stays zero.
                deltas[static_cast<std::size_t>(age)] = std::isnan(mean) ? 0.0 : expected - mean;
            }

            adjustments.emplace(sex, factor, std::move(deltas));
        }
    }

    return adjustments;
}

void AdjustableRiskFactorModel::adjust_risk_factors(
    RuntimeContext &context, sim::ScenarioJournal &journal,
    const std::vector<core::Identifier> &factors,
    const std::vector<core::DoubleInterval> *ranges, bool apply_trend,
    const AdjustmentScope &scope) const {
    sim::AdjustmentTable adjustments;

    if (context.scenario().type() == sim::ScenarioType::baseline) {
        adjustments = calculate_adjustments(context, factors, ranges, apply_trend, scope);
        journal.push_adjustment(context.current_run(), context.time_now(), adjustments);
    } else {
        adjustments = journal.pop_adjustment(context.current_run(), context.time_now());
    }

    // Applying a delta is per-person and independent, and draws nothing — but it is also cheap,
    // so it runs serially in slot order rather than earning a parallel region.
    for (auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }
        if (scope.income_stratum.has_value() &&
            (!person.has_income_adjustment_stratum ||
             person.income_adjustment_stratum != *scope.income_stratum)) {
            continue;
        }

        for (std::size_t i = 0; i < factors.size(); ++i) {
            const auto &factor = factors[i];
            if (!adjustments.contains(person.gender, factor)) {
                continue;
            }

            const auto &deltas = adjustments.at(person.gender, factor);
            if (static_cast<std::size_t>(person.age) >= deltas.size()) {
                continue;
            }

            const double delta = deltas.at(static_cast<std::size_t>(person.age));

            const auto current = factor_value(person, factor);
            if (!current.has_value()) {
                continue;
            }

            double adjusted = *current + delta;

            // Whether to clamp is the caller's choice, not the factor's, and the HLM path
            // passes no range. That is what makes `adjust_to_factors_mean` mean what it says:
            // shifting every value in an (age, sex) band by `expected - simulated_mean` lands
            // the band's mean exactly on the expected value, so the output reports the
            // FactorsMean table. Clamping the shifted values moves the mean back off it —
            // preferring the configured range here is what made the reference example's band
            // means disagree with the baseline's, most visibly at the young ages where the
            // expected BMI of 14 sits close to the configured lower bound of 13.88.
            //
            // PhysicalActivity is the exception the baseline makes, and for a stated reason: its
            // expected values and its model are on different scales, so its configured range
            // wins over the caller.
            if (factor == kPhysicalActivity && context.mapping().contains(factor) &&
                context.mapping().at(factor).range().has_value()) {
                adjusted = context.mapping().at(factor).get_bounded_value(adjusted);
            } else if (ranges != nullptr && i < ranges->size()) {
                adjusted = (*ranges)[i].clamp(adjusted);
            }

            person.risk_factors[factor] = adjusted;

            // Keep the dedicated members in step, since other modules read them.
            if (factor == kPhysicalActivity) {
                person.physical_activity = adjusted;
            } else if (factor == kIncome && person.income_continuous > 0.0) {
                person.income_continuous = adjusted;
            }
        }
    }
}

RiskFactorHostModule::RiskFactorHostModule(std::unique_ptr<RiskFactorModel> static_model,
                                           std::unique_ptr<RiskFactorModel> dynamic_model,
                                           sim::ScenarioJournal &journal)
    : static_model_{std::move(static_model)}, dynamic_model_{std::move(dynamic_model)},
      journal_{&journal} {
    if (!static_model_) {
        throw diag::InternalError("a risk factor host needs a static model");
    }
    if (!dynamic_model_) {
        throw diag::InternalError("a risk factor host needs a dynamic model");
    }
}

std::size_t RiskFactorHostModule::size() const noexcept { return 2; }

bool RiskFactorHostModule::contains(RiskFactorModelType model_type) const noexcept {
    switch (model_type) {
    case RiskFactorModelType::Static:
        return static_model_ != nullptr;
    case RiskFactorModelType::Dynamic:
        return dynamic_model_ != nullptr;
    }
    return false;
}

void RiskFactorHostModule::initialise_population(RuntimeContext &context) {
    static_model_->generate_risk_factors(context, *journal_);
    dynamic_model_->generate_risk_factors(context, *journal_);
}

void RiskFactorHostModule::update_population(RuntimeContext &context) {
    // Newborns are generated, not updated: they have no previous year to move from. The static
    // model runs first so the dynamic model's calibration sees them.
    static_model_->update_risk_factors(context, *journal_);
    dynamic_model_->update_risk_factors(context, *journal_);
}

} // namespace hgps::model
