// static_linear_model.cpp
#include "static_linear_model.h"

#include "data/population.h"
#include "hgps_core/diagnostics/internal_error.h"
#include "risk_factor_adjustable_model.h"
#include "simulation/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

using hgps::core::operator""_id;

namespace {

template <typename T> std::shared_ptr<T> create_shared_from_unique(std::unique_ptr<T> &ptr) {
    return ptr ? std::make_shared<T>(*ptr) : nullptr;
}

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw hgps::core::InternalError(message);
    }
}

template <typename T>
void require_size_match(const std::vector<T> &values, std::size_t expected,
                        const std::string &name) {
    if (values.size() != expected) {
        throw hgps::core::InternalError(name + " size mismatch");
    }
}

std::vector<hgps::core::Income>
ordered_income_categories(const std::unordered_map<hgps::core::Income, hgps::LinearModelParams>
                              &income_models,
                          int income_categories) {
    using Income = hgps::core::Income;

    std::vector<Income> ordered;
    if (income_categories == 4) {
        for (const auto income :
             {Income::low, Income::lowermiddle, Income::uppermiddle, Income::high}) {
            if (income_models.contains(income)) {
                ordered.push_back(income);
            }
        }
    } else {
        for (const auto income : {Income::low, Income::middle, Income::high}) {
            if (income_models.contains(income)) {
                ordered.push_back(income);
            }
        }
    }
    return ordered;
}

double safe_log(double value) {
    return std::log(std::max(value, 1e-10));
}

int parse_suffix_power(const std::string &name, const std::string &prefix) {
    if (name.size() <= prefix.size()) {
        return 1;
    }
    return std::stoi(name.substr(prefix.size()));
}

double evaluate_pattern_value(const hgps::Person &person, const std::string &factor_name) {
    using Gender = hgps::core::Gender;

    if (factor_name.starts_with("age")) {
        const int power = factor_name == "age" ? 1 : parse_suffix_power(factor_name, "age");
        return std::pow(static_cast<double>(person.age), power);
    }

    if (factor_name.starts_with("gender")) {
        if (factor_name == "gender") {
            return person.gender_to_value();
        }
        const int power = factor_name == "gender2" ? 1 : parse_suffix_power(factor_name, "gender");
        const double base = person.gender == Gender::male ? 1.0 : 0.0;
        return std::pow(base, power);
    }

    if (factor_name.starts_with("sector")) {
        const int power = factor_name == "sector" ? 1 : parse_suffix_power(factor_name, "sector");
        return std::pow(person.sector_to_value(), power);
    }

    if (factor_name == "region") {
        try {
            return std::stod(person.region);
        } catch (...) {
            return 0.0;
        }
    }

    if (factor_name.starts_with("region")) {
        return person.region == factor_name ? 1.0 : 0.0;
    }

    if (factor_name == "ethnicity") {
        try {
            return std::stod(person.ethnicity);
        } catch (...) {
            return 0.0;
        }
    }

    if (factor_name.starts_with("ethnicity")) {
        return person.ethnicity == factor_name ? 1.0 : 0.0;
    }

    if (factor_name == "income") {
        return static_cast<double>(person.income);
    }

    if (factor_name == "income_continuous") {
        return person.income_continuous;
    }

    if (factor_name.starts_with("income")) {
        const int power = parse_suffix_power(factor_name, "income");
        return std::pow(static_cast<double>(person.income), power);
    }

    if (factor_name == person.region || factor_name == person.ethnicity) {
        return 1.0;
    }

    return person.get_risk_factor_value(hgps::core::Identifier(factor_name));
}

double get_regression_value(const hgps::Person &person, const hgps::core::Identifier &factor) {
    return evaluate_pattern_value(person, factor.to_string());
}

} // namespace

namespace hgps {

StaticLinearModel::StaticLinearModel(
    std::shared_ptr<RiskFactorSexAgeTable> expected,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
    std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend_boxcox,
    const std::vector<core::Identifier> &names, const std::vector<LinearModelParams> &models,
    const std::vector<core::DoubleInterval> &ranges, const std::vector<double> &lambda,
    const std::vector<double> &stddev, const Eigen::MatrixXd &cholesky,
    const std::vector<LinearModelParams> &policy_models,
    const std::vector<core::DoubleInterval> &policy_ranges, const Eigen::MatrixXd &policy_cholesky,
    std::shared_ptr<std::vector<LinearModelParams>> trend_models,
    std::shared_ptr<std::vector<core::DoubleInterval>> trend_ranges,
    std::shared_ptr<std::vector<double>> trend_lambda, const double info_speed,
    const std::unordered_map<core::Identifier, std::unordered_map<core::Gender, double>>
        &rural_prevalence,
    const std::unordered_map<core::Income, LinearModelParams> &income_models,
    const double physical_activity_stddev, const TrendType trend_type,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_boxcox,
    std::shared_ptr<std::unordered_map<core::Identifier, int>> income_trend_steps,
    std::shared_ptr<std::vector<LinearModelParams>> income_trend_models,
    std::shared_ptr<std::vector<core::DoubleInterval>> income_trend_ranges,
    std::shared_ptr<std::vector<double>> income_trend_lambda,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> income_trend_decay_factors,
    const bool is_continuous_income_model, const LinearModelParams &continuous_income_model,
    const int income_categories,
    const std::unordered_map<core::Identifier, PhysicalActivityModel> &physical_activity_models,
    const bool has_active_policies, const std::vector<LinearModelParams> &logistic_models)
    : RiskFactorAdjustableModel{std::move(expected), expected_trend, trend_steps, trend_type,
                                expected_income_trend, income_trend_decay_factors},
      expected_trend_{std::move(expected_trend)},
      expected_trend_boxcox_{std::move(expected_trend_boxcox)},
      trend_models_{std::move(trend_models)},
      trend_ranges_{std::move(trend_ranges)},
      trend_lambda_{std::move(trend_lambda)},
      trend_type_{trend_type},
      expected_income_trend_{std::move(expected_income_trend)},
      expected_income_trend_boxcox_{std::move(expected_income_trend_boxcox)},
      income_trend_steps_{std::move(income_trend_steps)},
      income_trend_models_{std::move(income_trend_models)},
      income_trend_ranges_{std::move(income_trend_ranges)},
      income_trend_lambda_{std::move(income_trend_lambda)},
      income_trend_decay_factors_{std::move(income_trend_decay_factors)},
      names_{names},
      models_{models},
      ranges_{ranges},
      lambda_{lambda},
      stddev_{stddev},
      cholesky_{cholesky},
      policy_models_{policy_models},
      policy_ranges_{policy_ranges},
      policy_cholesky_{policy_cholesky},
      info_speed_{info_speed},
      rural_prevalence_{rural_prevalence},
      income_models_{income_models},
      physical_activity_stddev_{physical_activity_stddev},
      is_continuous_income_model_{is_continuous_income_model},
      continuous_income_model_{continuous_income_model},
      income_categories_{income_categories},
      physical_activity_models_{physical_activity_models},
      has_active_policies_{has_active_policies},
      logistic_models_{logistic_models} {
    require(!names_.empty(), "Risk factor names list is empty");
    require_size_match(models_, names_.size(), "Risk factor models");
    require_size_match(ranges_, names_.size(), "Risk factor ranges");
    require_size_match(lambda_, names_.size(), "Risk factor lambda");
    require_size_match(stddev_, names_.size(), "Risk factor stddev");
    require(cholesky_.allFinite(), "Risk factor Cholesky matrix contains non-finite values");

    require_size_match(policy_models_, names_.size(), "Policy models");
    require_size_match(policy_ranges_, names_.size(), "Policy ranges");
    require(policy_cholesky_.allFinite(),
            "Intervention policy Cholesky matrix contains non-finite values");

    require(logistic_models_.empty() || logistic_models_.size() == names_.size(),
            "Logistic models size mismatch");

    require(income_categories_ == 3 || income_categories_ == 4,
            "Income categories must be 3 or 4");

    if (!is_continuous_income_model_) {
        require(!income_models_.empty(), "Income models mapping is empty");
    }

    if (trend_type_ == TrendType::UPFTrend) {
        require(trend_models_ && trend_ranges_ && trend_lambda_, "UPF trend data is missing");
        require_size_match(*trend_models_, names_.size(), "UPF trend models");
        require_size_match(*trend_ranges_, names_.size(), "UPF trend ranges");
        require_size_match(*trend_lambda_, names_.size(), "UPF trend lambda");
        require(expected_trend_boxcox_ != nullptr, "UPF trend BoxCox values are missing");

        for (const auto &name : names_) {
            require(expected_trend_ && expected_trend_->contains(name),
                    "Expected trend value is missing");
            require(expected_trend_boxcox_->contains(name), "Expected trend BoxCox value is missing");
        }
    }

    if (trend_type_ == TrendType::IncomeTrend) {
        require(expected_income_trend_ && expected_income_trend_boxcox_ && income_trend_steps_ &&
                    income_trend_models_ && income_trend_ranges_ && income_trend_lambda_ &&
                    income_trend_decay_factors_,
                "Income trend data is missing");
        require_size_match(*income_trend_models_, names_.size(), "Income trend models");
        require_size_match(*income_trend_ranges_, names_.size(), "Income trend ranges");
        require_size_match(*income_trend_lambda_, names_.size(), "Income trend lambda");

        for (const auto &name : names_) {
            require(expected_income_trend_->contains(name),
                    "Expected income trend value is missing for risk factor: " + name.to_string());
            require(expected_income_trend_boxcox_->contains(name),
                    "Expected income trend BoxCox value is missing for risk factor: " +
                        name.to_string());
            require(income_trend_steps_->contains(name),
                    "Income trend steps value is missing for risk factor: " + name.to_string());
            require(income_trend_decay_factors_->contains(name),
                    "Income trend decay factor is missing for risk factor: " + name.to_string());
        }
    }
}

RiskFactorModelType StaticLinearModel::type() const noexcept { return RiskFactorModelType::Static; }

std::string StaticLinearModel::name() const noexcept { return "Static"; }

bool StaticLinearModel::is_continuous_income_model() const noexcept {
    return is_continuous_income_model_;
}

bool StaticLinearModel::has_logistic_model(const size_t risk_factor_index) const noexcept {
    return risk_factor_index < logistic_models_.size() &&
           !logistic_models_[risk_factor_index].coefficients.empty();
}

void StaticLinearModel::generate_risk_factors(RuntimeContext &context) {
    for (auto &person : context.population()) {
        initialise_sector(person, context.random());
    }

    if (is_continuous_income_model_) {
        for (auto &person : context.population()) {
            const double continuous_income = calculate_continuous_income(person, context.random());
            person.risk_factors["income"_id] = continuous_income;
            person.income_continuous = continuous_income;
            person.income = core::Income::unknown;
        }
    } else {
        for (auto &person : context.population()) {
            initialise_categorical_income(person, context.random());
        }
    }

    for (auto &person : context.population()) {
        initialise_factors(context, person, context.random());
        initialise_physical_activity(context, person, context.random());
    }

    std::unordered_set<core::Identifier> logistic_factors_set;
    for (size_t i = 0; i < names_.size(); ++i) {
        if (has_logistic_model(i)) {
            logistic_factors_set.insert(names_[i]);
        }
    }
    set_logistic_factors(logistic_factors_set);

    const auto &req = context.inputs().project_requirements();
    if (req.risk_factors.adjust_to_factors_mean) {
        auto [extended_factors, extended_ranges] =
            build_extended_factors_list(context, names_, ranges_);
        adjust_risk_factors(context, extended_factors,
                            extended_ranges.empty() ? std::nullopt
                                                    : OptionalRanges{extended_ranges},
                            false);
    }

    if (is_continuous_income_model_) {
        process_continuous_income_after_adjustment(context);
    }

    for (auto &person : context.population()) {
        if (has_active_policies_) {
            initialise_policies(context, person, context.random(), false);
        }

        if (!req.trend.enabled) {
            continue;
        }

        switch (trend_type_) {
        case TrendType::Null:
            break;
        case TrendType::UPFTrend:
            initialise_UPF_trends(context, person);
            break;
        case TrendType::IncomeTrend:
            initialise_income_trends(context, person);
            break;
        }
    }

    if (req.trend.enabled && req.risk_factors.trended) {
        auto [trended_extended_factors, trended_extended_ranges] =
            build_extended_factors_list(context, names_, ranges_, true);

        adjust_risk_factors(context, trended_extended_factors,
                            trended_extended_ranges.empty()
                                ? std::nullopt
                                : OptionalRanges{trended_extended_ranges},
                            true);

        if (is_continuous_income_model_) {
            process_continuous_income_after_adjustment(context);
        }
    }
}

void StaticLinearModel::update_risk_factors(RuntimeContext &context) {
    const auto policy_start_year = static_cast<int>(context.inputs().run().policy_start_year);
    const bool intervene = context.scenario().type() == ScenarioType::intervention &&
                           context.time_now() >= policy_start_year;

    for (auto &person : context.population()) {
        if (person.age == 0) {
            initialise_sector(person, context.random());
            initialise_income(context, person, context.random());
            initialise_factors(context, person, context.random());
            initialise_physical_activity(context, person, context.random());
        } else {
            update_sector(person, context.random());
            update_income(context, person, context.random());
            update_factors(context, person, context.random());
        }
    }

    const auto &req = context.inputs().project_requirements();
    if (req.risk_factors.adjust_to_factors_mean) {
        auto [extended_factors, extended_ranges] =
            build_extended_factors_list(context, names_, ranges_);
        adjust_risk_factors(context, extended_factors,
                            extended_ranges.empty() ? std::nullopt
                                                    : OptionalRanges{extended_ranges},
                            false);
    }

    if (is_continuous_income_model_) {
        process_continuous_income_after_adjustment(context);
    }

    for (auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }

        if (person.age == 0) {
            if (has_active_policies_) {
                initialise_policies(context, person, context.random(), intervene);
            }
            if (req.trend.enabled) {
                switch (trend_type_) {
                case TrendType::Null:
                    break;
                case TrendType::UPFTrend:
                    initialise_UPF_trends(context, person);
                    break;
                case TrendType::IncomeTrend:
                    initialise_income_trends(context, person);
                    break;
                }
            }
        } else {
            if (has_active_policies_) {
                update_policies(context, person, intervene);
            }
            if (req.trend.enabled) {
                switch (trend_type_) {
                case TrendType::Null:
                    break;
                case TrendType::UPFTrend:
                    update_UPF_trends(context, person);
                    break;
                case TrendType::IncomeTrend:
                    update_income_trends(context, person);
                    break;
                }
            }
        }
    }

    if (req.trend.enabled && req.risk_factors.trended) {
        auto [trended_extended_factors, trended_extended_ranges] =
            build_extended_factors_list(context, names_, ranges_, true);

        adjust_risk_factors(context, trended_extended_factors,
                            trended_extended_ranges.empty()
                                ? std::nullopt
                                : OptionalRanges{trended_extended_ranges},
                            true);

        if (is_continuous_income_model_) {
            process_continuous_income_after_adjustment(context);
        }
    }

    if (has_active_policies_) {
        for (auto &person : context.population()) {
            if (person.is_active()) {
                apply_policies(person, intervene);
            }
        }
    }
}

double StaticLinearModel::inverse_box_cox(const double factor, const double lambda) {
    if (std::abs(lambda) < 1e-10) {
        const double result = std::exp(factor);
        return std::isfinite(result) ? result : 0.0;
    }

    const double base = (lambda * factor) + 1.0;
    if (base <= 0.0) {
        return 0.0;
    }

    const double result = std::pow(base, 1.0 / lambda);
    return std::isfinite(result) ? std::max(0.0, result) : 0.0;
}

void StaticLinearModel::initialise_factors(RuntimeContext &context, Person &person,
                                           Random &random) const {
    const auto residuals = compute_residuals(random, cholesky_);
    const auto linear = compute_linear_models(context, person, models_);

    for (size_t i = 0; i < names_.size(); ++i) {
        const auto residual_name = core::Identifier{names_[i].to_string() + "_residual"};
        const double residual = residuals[i];
        person.risk_factors[residual_name] = residual;

        const double expected =
            get_expected(context, person.gender, person.age, names_[i], ranges_[i], false);

        if (has_logistic_model(i)) {
            const double zero_probability = calculate_zero_probability(person, i);
            if (random.next_double() < zero_probability) {
                person.risk_factors[names_[i]] = 0.0;
                continue;
            }
        }

        double factor = linear[i] + residual * stddev_[i];
        factor = expected * inverse_box_cox(factor, lambda_[i]);
        factor = ranges_[i].clamp(factor);
        person.risk_factors[names_[i]] = factor;
    }
}

void StaticLinearModel::update_factors(RuntimeContext &context, Person &person,
                                       Random &random) const {
    const auto new_residuals = compute_residuals(random, cholesky_);
    const auto linear = compute_linear_models(context, person, models_);

    for (size_t i = 0; i < names_.size(); ++i) {
        const double expected =
            get_expected(context, person.gender, person.age, names_[i], ranges_[i], false);

        const auto residual_name = core::Identifier{names_[i].to_string() + "_residual"};
        const double residual_old = person.risk_factors.at(residual_name);
        double residual = new_residuals[i] * info_speed_;
        residual += std::sqrt(1.0 - info_speed_ * info_speed_) * residual_old;
        person.risk_factors.at(residual_name) = residual;

        if (has_logistic_model(i)) {
            double zero_probability = calculate_zero_probability(person, i);
            zero_probability = (zero_probability + (person.risk_factors.at(names_[i]) == 0.0 ? 1.0 : 0.0)) / 2.0;
            if (random.next_double() < zero_probability) {
                person.risk_factors.at(names_[i]) = 0.0;
                continue;
            }
        }

        double factor = linear[i] + residual * stddev_[i];
        factor = expected * inverse_box_cox(factor, lambda_[i]);
        factor = ranges_[i].clamp(factor);
        person.risk_factors.at(names_[i]) = factor;
    }
}

void StaticLinearModel::initialise_UPF_trends(RuntimeContext &context, Person &person) const {
    const auto linear = compute_linear_models(context, person, *trend_models_);

    for (size_t i = 0; i < names_.size(); ++i) {
        const auto trend_name = core::Identifier{names_[i].to_string() + "_trend"};
        double trend = expected_trend_boxcox_->at(names_[i]) *
                       inverse_box_cox(linear[i], (*trend_lambda_)[i]);
        trend = (*trend_ranges_)[i].clamp(trend);
        person.risk_factors[trend_name] = trend;
    }

    update_UPF_trends(context, person);
}

void StaticLinearModel::update_UPF_trends(RuntimeContext &context, Person &person) const {
    const int elapsed_time = context.time_now() - context.start_time();

    for (size_t i = 0; i < names_.size(); ++i) {
        const auto trend_name = core::Identifier{names_[i].to_string() + "_trend"};
        const double trend = person.risk_factors.at(trend_name);

        double factor = person.risk_factors.at(names_[i]);
        const int t = std::min(elapsed_time, get_trend_steps(names_[i]));
        factor *= std::pow(trend, t);
        person.risk_factors.at(names_[i]) = ranges_[i].clamp(factor);
    }
}

void StaticLinearModel::initialise_income_trends(RuntimeContext &context, Person &person) const {
    if (!income_trend_models_ || !expected_income_trend_boxcox_ || !income_trend_lambda_ ||
        !income_trend_ranges_) {
        return;
    }

    const auto linear = compute_linear_models(context, person, *income_trend_models_);
    for (size_t i = 0; i < names_.size(); ++i) {
        const auto trend_name = core::Identifier{names_[i].to_string() + "_income_trend"};
        double trend = expected_income_trend_boxcox_->at(names_[i]) *
                       inverse_box_cox(linear[i], (*income_trend_lambda_)[i]);
        trend = (*income_trend_ranges_)[i].clamp(trend);
        person.risk_factors[trend_name] = trend;
    }

    update_income_trends(context, person);
}

void StaticLinearModel::update_income_trends(RuntimeContext &context, Person &person) const {
    if (!income_trend_models_ || !expected_income_trend_boxcox_ || !income_trend_lambda_ ||
        !income_trend_ranges_ || !income_trend_decay_factors_ || !income_trend_steps_) {
        return;
    }

    const int elapsed_time = context.time_now() - context.start_time();
    if (elapsed_time <= 0) {
        return;
    }

    for (size_t i = 0; i < names_.size(); ++i) {
        const auto trend_name = core::Identifier{names_[i].to_string() + "_income_trend"};
        if (!person.risk_factors.contains(trend_name)) {
            continue;
        }

        const double trend = person.risk_factors.at(trend_name);
        const double decay_factor = income_trend_decay_factors_->at(names_[i]);
        const int t = std::min(elapsed_time, income_trend_steps_->at(names_[i]));

        double factor = person.risk_factors.at(names_[i]);
        factor *= trend * std::exp(decay_factor * t);
        person.risk_factors.at(names_[i]) = ranges_[i].clamp(factor);
    }
}

void StaticLinearModel::initialise_policies(RuntimeContext &context, Person &person, Random &random,
                                            const bool intervene) const {
    if (!has_active_policies_) {
        return;
    }

    const auto residuals = compute_residuals(random, policy_cholesky_);
    for (size_t i = 0; i < names_.size(); ++i) {
        const auto residual_name = core::Identifier{names_[i].to_string() + "_policy_residual"};
        person.risk_factors[residual_name] = residuals[i];
    }

    update_policies(context, person, intervene);
}

void StaticLinearModel::update_policies(RuntimeContext &context, Person &person,
                                        const bool intervene) const {
    if (!has_active_policies_) {
        return;
    }

    if (!intervene) {
        for (const auto &name : names_) {
            person.risk_factors[core::Identifier{name.to_string() + "_policy"}] = 0.0;
        }
        return;
    }

    const auto linear = compute_linear_models(context, person, policy_models_);
    for (size_t i = 0; i < names_.size(); ++i) {
        const auto residual_name = core::Identifier{names_[i].to_string() + "_policy_residual"};
        const auto policy_name = core::Identifier{names_[i].to_string() + "_policy"};
        double policy = linear[i] + person.risk_factors.at(residual_name);
        person.risk_factors[policy_name] = policy_ranges_[i].clamp(policy);
    }
}

void StaticLinearModel::apply_policies(Person &person, const bool intervene) const {
    if (!has_active_policies_ || !intervene) {
        return;
    }

    for (size_t i = 0; i < names_.size(); ++i) {
        const auto policy_name = core::Identifier{names_[i].to_string() + "_policy"};
        const double policy = person.risk_factors.at(policy_name);
        double factor = person.risk_factors.at(names_[i]);
        factor *= 1.0 + policy / 100.0;
        person.risk_factors.at(names_[i]) = ranges_[i].clamp(factor);
    }
}

std::vector<double>
StaticLinearModel::compute_linear_models(RuntimeContext &context, Person &person,
                                         const std::vector<LinearModelParams> &models) const {
    std::vector<double> linear;
    linear.reserve(names_.size());

    const double age_for_models = [&]() {
        const auto &max_age_opt =
            context.inputs().project_requirements().demographics.max_age_for_linear_models;
        if (max_age_opt.has_value() && *max_age_opt > 0) {
            return std::min(static_cast<double>(person.age), static_cast<double>(*max_age_opt));
        }
        return static_cast<double>(person.age);
    }();

    const double age_squared = age_for_models * age_for_models;
    const double age_cubed = age_squared * age_for_models;

    static const core::Identifier age_id("age");
    static const core::Identifier age2_id("age2");
    static const core::Identifier age3_id("age3");
    static const core::Identifier Age_id("Age");
    static const core::Identifier Age2_id("Age2");
    static const core::Identifier Age3_id("Age3");
    static const core::Identifier stddev_id("stddev");
    static const core::Identifier log_energy_intake_id("log_energy_intake");
    static const core::Identifier energyintake_id("energyintake");

    for (size_t i = 0; i < names_.size(); ++i) {
        const auto &model = models.at(i);
        double factor = model.intercept;

        for (const auto &[coefficient_name, coefficient_value] : model.coefficients) {
            if (coefficient_name == stddev_id) {
                continue;
            }

            if (coefficient_name == age_id || coefficient_name == Age_id) {
                factor += coefficient_value * age_for_models;
                continue;
            }
            if (coefficient_name == age2_id || coefficient_name == Age2_id) {
                factor += coefficient_value * age_squared;
                continue;
            }
            if (coefficient_name == age3_id || coefficient_name == Age3_id) {
                factor += coefficient_value * age_cubed;
                continue;
            }

            try {
                factor += coefficient_value * person.get_risk_factor_value(coefficient_name);
            } catch (const std::exception &) {
                if (coefficient_name == log_energy_intake_id) {
                    factor += coefficient_value *
                              safe_log(get_expected(context, person.gender, person.age,
                                                    energyintake_id, std::nullopt, false));
                } else {
                    factor += coefficient_value *
                              get_expected(context, person.gender, person.age, coefficient_name,
                                           std::nullopt, false);
                }
            }
        }

        for (const auto &[coefficient_name, coefficient_value] : model.log_coefficients) {
            try {
                factor += coefficient_value *
                          safe_log(person.get_risk_factor_value(coefficient_name));
            } catch (const std::exception &) {
                if (coefficient_name == log_energy_intake_id) {
                    factor += coefficient_value *
                              safe_log(get_expected(context, person.gender, person.age,
                                                    energyintake_id, std::nullopt, false));
                } else {
                    factor += coefficient_value *
                              safe_log(get_expected(context, person.gender, person.age,
                                                    coefficient_name, std::nullopt, false));
                }
            }
        }

        linear.push_back(factor);
    }

    return linear;
}

double StaticLinearModel::calculate_zero_probability(Person &person,
                                                     const size_t risk_factor_index) const {
    if (!has_logistic_model(risk_factor_index)) {
        return 0.0;
    }

    const auto &logistic_model = logistic_models_[risk_factor_index];
    double linear_term = logistic_model.intercept;
    for (const auto &[coef_name, coef_value] : logistic_model.coefficients) {
        linear_term += coef_value * person.get_risk_factor_value(coef_name);
    }

    const double probability = 1.0 / (1.0 + std::exp(-linear_term));
    return std::clamp(probability, 0.0, 1.0);
}

std::vector<double> StaticLinearModel::compute_residuals(Random &random,
                                                         const Eigen::MatrixXd &cholesky) const {
    std::vector<double> correlated_residuals;
    correlated_residuals.reserve(names_.size());

    Eigen::VectorXd residuals{static_cast<Eigen::Index>(names_.size())};
    std::ranges::generate(residuals, [&random] { return random.next_normal(0.0, 1.0); });
    residuals = cholesky * residuals;

    for (size_t i = 0; i < names_.size(); ++i) {
        correlated_residuals.push_back(residuals[static_cast<Eigen::Index>(i)]);
    }

    return correlated_residuals;
}

void StaticLinearModel::initialise_sector(Person &person, Random &random) const {
    if (rural_prevalence_.empty()) {
        return;
    }

    const double prevalence =
        person.age < 18 ? rural_prevalence_.at("Under18"_id).at(person.gender)
                        : rural_prevalence_.at("Over18"_id).at(person.gender);

    person.sector = random.next_double() < prevalence ? core::Sector::rural : core::Sector::urban;
}

void StaticLinearModel::update_sector(Person &person, Random &random) const {
    if (rural_prevalence_.empty()) {
        return;
    }

    if (person.age != 18 || person.sector != core::Sector::rural) {
        return;
    }

    const double prevalence_under18 = rural_prevalence_.at("Under18"_id).at(person.gender);
    const double prevalence_over18 = rural_prevalence_.at("Over18"_id).at(person.gender);
    const double p_rural_to_urban = 1.0 - prevalence_over18 / prevalence_under18;
    if (random.next_double() < p_rural_to_urban) {
        person.sector = core::Sector::urban;
    }
}

void StaticLinearModel::initialise_income(RuntimeContext &context, Person &person, Random &random) {
    if (is_continuous_income_model_) {
        const double continuous_income = calculate_continuous_income(person, context.random());
        person.risk_factors["income"_id] = continuous_income;
        person.income_continuous = continuous_income;
        person.income = core::Income::unknown;
    } else {
        initialise_categorical_income(person, random);
    }
}

void StaticLinearModel::update_income(RuntimeContext &context, Person &person, Random &random) {
    if (person.age == 18) {
        initialise_income(context, person, random);
    }
}

void StaticLinearModel::initialise_categorical_income(Person &person, Random &random) {
    const auto ordered = ordered_income_categories(income_models_, income_categories_);
    require(!ordered.empty(), "No categorical income models available");

    std::vector<std::pair<core::Income, double>> logits;
    logits.reserve(ordered.size());

    double max_logit = -std::numeric_limits<double>::infinity();
    for (const auto income : ordered) {
        const auto &params = income_models_.at(income);
        double logit = params.intercept;
        for (const auto &[factor, coefficient] : params.coefficients) {
            logit += coefficient * person.get_risk_factor_value(factor);
        }
        max_logit = std::max(max_logit, logit);
        logits.emplace_back(income, logit);
    }

    double sum = 0.0;
    std::vector<std::pair<core::Income, double>> probs;
    probs.reserve(logits.size());
    for (const auto &[income, logit] : logits) {
        const double value = std::exp(logit - max_logit);
        probs.emplace_back(income, value);
        sum += value;
    }

    const double draw = random.next_double();
    double cumulative = 0.0;
    for (size_t i = 0; i < probs.size(); ++i) {
        cumulative += probs[i].second / sum;
        if (draw < cumulative || i + 1 == probs.size()) {
            person.income = probs[i].first;
            person.risk_factors["income"_id] = static_cast<double>(person.income_to_value());
            return;
        }
    }

    throw core::InternalError("Failed to initialise categorical income category");
}

double StaticLinearModel::calculate_continuous_income(Person &person, Random &random) {
    double income = continuous_income_model_.intercept;

    for (const auto &[factor, coefficient] : continuous_income_model_.coefficients) {
        const auto name = factor.to_string();
        if (name == "IncomeContinuousStdDev" || name == "stddev" || name == "min" ||
            name == "max") {
            continue;
        }

        try {
            income += coefficient * get_regression_value(person, factor);
        } catch (const std::exception &) {
            continue;
        }
    }

    double noise_stddev = 0.0;
    if (const auto it = continuous_income_model_.coefficients.find("IncomeContinuousStdDev"_id);
        it != continuous_income_model_.coefficients.end()) {
        noise_stddev = it->second;
    }

    if (noise_stddev > 0.0) {
        const double noise = random.next_normal(0.0, noise_stddev);
        person.risk_factors["income_continuous_residual"_id] = noise;
        income += noise;
    }

    if (const auto min_it = continuous_income_model_.coefficients.find("min"_id);
        min_it != continuous_income_model_.coefficients.end()) {
        income = std::max(income, min_it->second);
    }
    if (const auto max_it = continuous_income_model_.coefficients.find("max"_id);
        max_it != continuous_income_model_.coefficients.end()) {
        income = std::min(income, max_it->second);
    }

    return income;
}

std::vector<double> StaticLinearModel::calculate_income_quartiles(const Population &population) {
    std::vector<double> incomes;
    incomes.reserve(population.size());

    for (const auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        if (const auto it = person.risk_factors.find("income"_id); it != person.risk_factors.end()) {
            incomes.push_back(it->second);
        }
    }

    require(!incomes.empty(), "No continuous income values found for quartile calculation");

    std::ranges::sort(incomes);
    const size_t n = incomes.size();

    return {
        incomes[static_cast<size_t>(std::round((n - 1) * 0.25))],
        incomes[static_cast<size_t>(std::round((n - 1) * 0.50))],
        incomes[static_cast<size_t>(std::round((n - 1) * 0.75))],
    };
}

std::vector<double> StaticLinearModel::calculate_income_tertiles(const Population &population) {
    std::vector<double> incomes;
    incomes.reserve(population.size());

    for (const auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        if (const auto it = person.risk_factors.find("income"_id); it != person.risk_factors.end()) {
            incomes.push_back(it->second);
        }
    }

    require(!incomes.empty(), "No continuous income values found for tertile calculation");

    std::ranges::sort(incomes);
    const size_t n = incomes.size();

    return {
        incomes[static_cast<size_t>(std::round((n - 1) * 0.33))],
        incomes[static_cast<size_t>(std::round((n - 1) * 0.67))],
    };
}

std::vector<double> StaticLinearModel::calculate_income_thresholds(
    const Population &population) const {
    return income_categories_ == 4 ? calculate_income_quartiles(population)
                                   : calculate_income_tertiles(population);
}

core::Income
StaticLinearModel::convert_income_to_category(const double continuous_income,
                                              const std::vector<double> &thresholds) const {
    if (income_categories_ == 4) {
        require(thresholds.size() == 3, "Invalid quartile thresholds");
        if (continuous_income <= thresholds[0]) {
            return core::Income::low;
        }
        if (continuous_income <= thresholds[1]) {
            return core::Income::lowermiddle;
        }
        if (continuous_income <= thresholds[2]) {
            return core::Income::uppermiddle;
        }
        return core::Income::high;
    }

    require(thresholds.size() == 2, "Invalid tertile thresholds");
    if (continuous_income <= thresholds[0]) {
        return core::Income::low;
    }
    if (continuous_income <= thresholds[1]) {
        return core::Income::middle;
    }
    return core::Income::high;
}

void StaticLinearModel::clamp_continuous_income_to_expected_range(RuntimeContext &context) const {
    const core::Identifier income_id("income");
    double income_min = std::numeric_limits<double>::max();
    double income_max = std::numeric_limits<double>::lowest();

    const auto age_range = context.inputs().settings().age_range();
    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        for (const auto sex : {core::Gender::male, core::Gender::female}) {
            try {
                const double expected =
                    get_expected(context, sex, age, income_id, std::nullopt, false);
                income_min = std::min(income_min, expected);
                income_max = std::max(income_max, expected);
            } catch (const std::exception &) {
            }
        }
    }

    if (income_min > income_max) {
        return;
    }

    for (auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }
        if (!person.risk_factors.contains(income_id)) {
            continue;
        }

        const double clamped = std::clamp(person.risk_factors.at(income_id), income_min, income_max);
        person.risk_factors[income_id] = clamped;
        if (person.income_continuous > 0.0) {
            person.income_continuous = clamped;
        }
    }
}

void StaticLinearModel::assign_continuous_income_categories(RuntimeContext &context) const {
    const auto thresholds = calculate_income_thresholds(context.population());

    for (auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }
        if (!person.risk_factors.contains("income"_id)) {
            continue;
        }

        person.income = convert_income_to_category(person.risk_factors.at("income"_id), thresholds);
    }
}

void StaticLinearModel::process_continuous_income_after_adjustment(RuntimeContext &context) const {
    clamp_continuous_income_to_expected_range(context);
    assign_continuous_income_categories(context);
}

void StaticLinearModel::initialise_physical_activity(RuntimeContext &context, Person &person,
                                                     Random &random) const {
    const auto &pa_req = context.inputs().project_requirements().physical_activity;
    if (!pa_req.enabled) {
        return;
    }

    if (pa_req.type == "simple") {
        const auto it = physical_activity_models_.find(core::Identifier("simple"));
        if (it != physical_activity_models_.end()) {
            initialise_simple_physical_activity(context, person, random, it->second);
        } else {
            PhysicalActivityModel model;
            model.model_type = "simple";
            model.stddev = physical_activity_stddev_;
            initialise_simple_physical_activity(context, person, random, model);
        }
        return;
    }

    const auto it = physical_activity_models_.find(core::Identifier("continuous"));
    if (it == physical_activity_models_.end()) {
        throw core::InternalError(
            "continuous physical activity requested but no continuous model is loaded");
    }

    initialise_continuous_physical_activity(context, person, random, it->second);
}

void StaticLinearModel::initialise_continuous_physical_activity(
    RuntimeContext & /*context*/, Person &person, Random &random, const PhysicalActivityModel &model) {
    double value = model.intercept;

    for (const auto &[factor_name, coefficient] : model.coefficients) {
        const auto name = factor_name.to_string();
        if (name == "stddev" || name == "min" || name == "max") {
            continue;
        }

        try {
            value += coefficient * get_regression_value(person, factor_name);
        } catch (const std::exception &) {
            continue;
        }
    }

    value += random.next_normal(0.0, model.stddev);
    value = std::clamp(value, model.min_value, model.max_value);

    person.physical_activity = value;
    person.risk_factors["PhysicalActivity"_id] = value;
}

void StaticLinearModel::initialise_simple_physical_activity(
    RuntimeContext &context, Person &person, Random &random,
    const PhysicalActivityModel &model) const {
    const double expected = get_expected(context, person.gender, person.age, "PhysicalActivity"_id,
                                         std::nullopt, false);
    const double rand = random.next_normal(0.0, model.stddev);
    const double factor = expected * std::exp(rand - (0.5 * std::pow(model.stddev, 2)));

    person.physical_activity = factor;
    person.risk_factors["PhysicalActivity"_id] = factor;
}

std::pair<std::vector<core::Identifier>, std::vector<core::DoubleInterval>>
StaticLinearModel::build_extended_factors_list(RuntimeContext &context,
                                               const std::vector<core::Identifier> &base_factors,
                                               const std::vector<core::DoubleInterval> &base_ranges,
                                               const bool for_trended_adjustment) const {
    const auto &req = context.inputs().project_requirements();

    std::vector<core::Identifier> extended_factors = base_factors;
    std::vector<core::DoubleInterval> extended_ranges = base_ranges;

    const core::Identifier income_id("income");
    const core::Identifier physical_activity_id("PhysicalActivity");

    const bool income_in_base =
        std::ranges::find(base_factors, income_id) != base_factors.end();
    const bool pa_in_base =
        std::ranges::find(base_factors, physical_activity_id) != base_factors.end();

    const bool add_income =
        for_trended_adjustment ? req.income.trended : req.income.adjust_to_factors_mean;
    if (!income_in_base && req.income.enabled && add_income) {
        try {
            get_expected(context, core::Gender::male, 0, income_id, std::nullopt, false);
            extended_factors.push_back(income_id);
            if (!base_ranges.empty()) {
                extended_ranges.emplace_back(0.0, 1e9);
            }
        } catch (const std::exception &) {
        }
    }

    const bool add_pa = for_trended_adjustment ? req.physical_activity.trended
                                               : req.physical_activity.adjust_to_factors_mean;
    if (!pa_in_base && req.physical_activity.enabled && add_pa) {
        try {
            get_expected(context, core::Gender::male, 0, physical_activity_id, std::nullopt, false);
            extended_factors.push_back(physical_activity_id);
            if (!base_ranges.empty()) {
                extended_ranges.push_back(base_ranges.back());
            }
        } catch (const std::exception &) {
        }
    }

    return {std::move(extended_factors), std::move(extended_ranges)};
}

StaticLinearModelDefinition::StaticLinearModelDefinition(
    std::unique_ptr<RiskFactorSexAgeTable> expected,
    std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
    std::unique_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
    std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend_boxcox,
    std::vector<core::Identifier> names, std::vector<LinearModelParams> models,
    std::vector<core::DoubleInterval> ranges, std::vector<double> lambda,
    std::vector<double> stddev, Eigen::MatrixXd cholesky,
    std::vector<LinearModelParams> policy_models, std::vector<core::DoubleInterval> policy_ranges,
    Eigen::MatrixXd policy_cholesky, std::unique_ptr<std::vector<LinearModelParams>> trend_models,
    std::unique_ptr<std::vector<core::DoubleInterval>> trend_ranges,
    std::unique_ptr<std::vector<double>> trend_lambda, const double info_speed,
    std::unordered_map<core::Identifier, std::unordered_map<core::Gender, double>> rural_prevalence,
    std::unordered_map<core::Income, LinearModelParams> income_models,
    const double physical_activity_stddev, const TrendType trend_type,
    std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend,
    std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_boxcox,
    std::unique_ptr<std::unordered_map<core::Identifier, int>> income_trend_steps,
    std::unique_ptr<std::vector<LinearModelParams>> income_trend_models,
    std::unique_ptr<std::vector<core::DoubleInterval>> income_trend_ranges,
    std::unique_ptr<std::vector<double>> income_trend_lambda,
    std::unique_ptr<std::unordered_map<core::Identifier, double>> income_trend_decay_factors,
    const bool is_continuous_income_model, const LinearModelParams &continuous_income_model,
    const int income_categories,
    const std::unordered_map<core::Identifier, PhysicalActivityModel> &physical_activity_models,
    const bool has_active_policies, std::vector<LinearModelParams> logistic_models)
    : RiskFactorAdjustableModelDefinition{
          std::move(expected),
          expected_trend ? std::make_unique<std::unordered_map<core::Identifier, double>>(
                               *expected_trend)
                         : nullptr,
          trend_steps ? std::make_unique<std::unordered_map<core::Identifier, int>>(*trend_steps)
                      : nullptr,
          trend_type},
      expected_trend_{
          expected_trend
              ? std::make_shared<std::unordered_map<core::Identifier, double>>(*expected_trend)
              : nullptr},
      expected_trend_boxcox_{expected_trend_boxcox
                                 ? std::make_shared<std::unordered_map<core::Identifier, double>>(
                                       *expected_trend_boxcox)
                                 : nullptr},
      trend_models_{create_shared_from_unique(trend_models)},
      trend_ranges_{create_shared_from_unique(trend_ranges)},
      trend_lambda_{create_shared_from_unique(trend_lambda)},
      trend_type_{trend_type},
      expected_income_trend_{expected_income_trend
                                 ? std::make_shared<std::unordered_map<core::Identifier, double>>(
                                       std::move(*expected_income_trend))
                                 : nullptr},
      expected_income_trend_boxcox_{
          expected_income_trend_boxcox
              ? std::make_shared<std::unordered_map<core::Identifier, double>>(
                    std::move(*expected_income_trend_boxcox))
              : nullptr},
      income_trend_steps_{income_trend_steps
                              ? std::make_shared<std::unordered_map<core::Identifier, int>>(
                                    std::move(*income_trend_steps))
                              : nullptr},
      income_trend_models_{income_trend_models
                               ? std::make_shared<std::vector<LinearModelParams>>(
                                     std::move(*income_trend_models))
                               : nullptr},
      income_trend_ranges_{income_trend_ranges
                               ? std::make_shared<std::vector<core::DoubleInterval>>(
                                     std::move(*income_trend_ranges))
                               : nullptr},
      income_trend_lambda_{income_trend_lambda
                               ? std::make_shared<std::vector<double>>(
                                     std::move(*income_trend_lambda))
                               : nullptr},
      income_trend_decay_factors_{
          income_trend_decay_factors
              ? std::make_shared<std::unordered_map<core::Identifier, double>>(
                    std::move(*income_trend_decay_factors))
              : nullptr},
      names_{std::move(names)},
      models_{std::move(models)},
      ranges_{std::move(ranges)},
      lambda_{std::move(lambda)},
      stddev_{std::move(stddev)},
      cholesky_{std::move(cholesky)},
      policy_models_{std::move(policy_models)},
      policy_ranges_{std::move(policy_ranges)},
      policy_cholesky_{std::move(policy_cholesky)},
      info_speed_{info_speed},
      rural_prevalence_{std::move(rural_prevalence)},
      income_models_{std::move(income_models)},
      physical_activity_stddev_{physical_activity_stddev},
      physical_activity_models_{physical_activity_models},
      logistic_models_{std::move(logistic_models)},
      is_continuous_income_model_{is_continuous_income_model},
      continuous_income_model_{continuous_income_model},
      income_categories_{income_categories},
      has_active_policies_{has_active_policies} {
    require(!names_.empty(), "Risk factor names list is empty");
    require_size_match(models_, names_.size(), "Risk factor models");
    require_size_match(ranges_, names_.size(), "Risk factor ranges");
    require_size_match(lambda_, names_.size(), "Risk factor lambda");
    require_size_match(stddev_, names_.size(), "Risk factor stddev");
    require(cholesky_.allFinite(), "Risk factor Cholesky matrix contains non-finite values");

    require_size_match(policy_models_, names_.size(), "Policy models");
    require_size_match(policy_ranges_, names_.size(), "Policy ranges");
    require(policy_cholesky_.allFinite(),
            "Intervention policy Cholesky matrix contains non-finite values");

    require(logistic_models_.empty() || logistic_models_.size() == names_.size(),
            "Logistic models size mismatch");
}

std::unique_ptr<RiskFactorModel> StaticLinearModelDefinition::create_model() const {
    return std::make_unique<StaticLinearModel>(
        expected_, expected_trend_, trend_steps_, expected_trend_boxcox_, names_, models_, ranges_,
        lambda_, stddev_, cholesky_, policy_models_, policy_ranges_, policy_cholesky_,
        trend_models_, trend_ranges_, trend_lambda_, info_speed_, rural_prevalence_, income_models_,
        physical_activity_stddev_, trend_type_, expected_income_trend_,
        expected_income_trend_boxcox_, income_trend_steps_, income_trend_models_,
        income_trend_ranges_, income_trend_lambda_, income_trend_decay_factors_,
        is_continuous_income_model_, continuous_income_model_, income_categories_,
        physical_activity_models_, has_active_policies_, logistic_models_);
}

} // namespace hgps