// The StaticLinear model's lifecycle: construction, and the order the pieces run in.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: StaticLinearModel::generate_risk_factors / update_risk_factors in
//         src/HealthGPS/static_linear_model.cpp.
#include "static_linear_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"
#include "sim/scenario.h"

#include <algorithm>
#include <string>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kIncome{"income"};
const core::Identifier kPhysicalActivity{"physicalactivity"};

/// Income is on a different scale from every declared risk factor — the FINCH pack's values run
/// to the thousands while the factors are bounded by their config ranges — so the calibration
/// pass over income is given a range wide enough not to clamp anything. The baseline uses the
/// same value and says why: clamping income to another factor's range collapses the quantiles.
const core::DoubleInterval kIncomeAdjustmentRange{0.0, 1e9};

} // namespace

void resolve_static_linear_predictors(StaticLinearParameters &parameters) {
    const auto resolve_each = [](std::vector<LinearModelParams> &models) {
        for (auto &model : models) {
            resolve_predictors(model);
        }
    };

    resolve_each(parameters.models);
    resolve_each(parameters.policy_models);
    resolve_each(parameters.logistic_models);
    resolve_each(parameters.trend_models);
    resolve_each(parameters.income_trend_models);
    resolve_predictors(parameters.continuous_income_model);
    resolve_predictors(parameters.physical_activity.linear);
    for (auto &[income, model] : parameters.income_models) {
        resolve_predictors(model);
    }
}

StaticLinearModel::StaticLinearModel(
    std::shared_ptr<const SexAgeFactorTable> expected,
    std::shared_ptr<const std::map<core::Identifier, double>> trend,
    std::shared_ptr<const std::map<core::Identifier, int>> trend_steps,
    std::shared_ptr<const StaticLinearParameters> parameters,
    std::shared_ptr<const std::map<core::Identifier, double>> decay)
    : AdjustableRiskFactorModel{std::move(expected), std::move(trend), std::move(trend_steps),
                                parameters ? parameters->trend_type : TrendType::Null,
                                std::move(decay)},
      parameters_{std::move(parameters)} {
    if (!parameters_) {
        throw diag::InternalError("the static linear model needs its parameters");
    }

    const auto factors = parameters_->names.size();
    if (factors == 0) {
        throw diag::InternalError("the static linear model has no risk factors");
    }

    // The derived names, once per model rather than once per factor per person per year.
    const auto suffixed = [this](std::string_view suffix) {
        std::vector<core::Identifier> keys;
        keys.reserve(parameters_->names.size());
        for (const auto &factor : parameters_->names) {
            keys.emplace_back(factor.to_string() + std::string{suffix});
        }
        return keys;
    };
    derived_keys_.residual = suffixed("_residual");
    derived_keys_.policy_residual = suffixed("_policy_residual");
    derived_keys_.policy = suffixed("_policy");
    derived_keys_.trend = suffixed("_trend");
    derived_keys_.income_trend = suffixed("_income_trend");

    // Every per-factor vector is indexed by the same position, so a length mismatch is a silent
    // misalignment of coefficients to factors rather than an out-of-range access.
    const auto check = [&](std::string_view what, std::size_t size) {
        if (size != factors) {
            throw diag::InternalError(
                fmt::format("the static linear model has {} risk factors but {} {}", factors,
                            size, what));
        }
    };
    check("models", parameters_->models.size());
    check("ranges", parameters_->ranges.size());
    check("lambda values", parameters_->lambda.size());
    check("standard deviations", parameters_->stddev.size());
    check("policy models", parameters_->policy_models.size());
    check("policy ranges", parameters_->policy_ranges.size());
    check("logistic models", parameters_->logistic_models.size());

    if (parameters_->cholesky.rows() != factors || parameters_->cholesky.columns() != factors) {
        throw diag::InternalError(
            fmt::format("the risk factor Cholesky factor is {}x{} for {} risk factors",
                        parameters_->cholesky.rows(), parameters_->cholesky.columns(), factors));
    }
    if (parameters_->policy_cholesky.rows() != factors ||
        parameters_->policy_cholesky.columns() != factors) {
        throw diag::InternalError(
            fmt::format("the policy Cholesky factor is {}x{} for {} risk factors",
                        parameters_->policy_cholesky.rows(),
                        parameters_->policy_cholesky.columns(), factors));
    }

    if (parameters_->info_speed < 0.0 || parameters_->info_speed > 1.0) {
        throw diag::InternalError(fmt::format(
            "the information speed must be in [0, 1], not {}", parameters_->info_speed));
    }

    // The factors whose zeros the calibration must leave out of the simulated mean: a two-stage
    // factor's zero means "does not apply to this person", not "a very small amount".
    std::vector<core::Identifier> logistic;
    for (std::size_t i = 0; i < factors; ++i) {
        if (!parameters_->logistic_models[i].coefficients.empty()) {
            logistic.push_back(parameters_->names[i]);
        }
    }
    set_logistic_factors(std::move(logistic));
}

AssignedAttributes StaticLinearModel::assigns() const noexcept {
    AssignedAttributes attributes;
    attributes.income_category = parameters_->income_enabled;
    attributes.income = parameters_->income_enabled;
    attributes.physical_activity = parameters_->physical_activity_enabled;
    attributes.sector = !parameters_->rural_prevalence.empty();
    // Region and ethnicity are the demographic module's business; whether they are assigned is a
    // project requirement, and the model loader refuses a config that asks for them without the
    // prevalence files to do it with.
    return attributes;
}

std::vector<core::Identifier> StaticLinearModel::generated_factors() const {
    return parameters_->names;
}

bool StaticLinearModel::stratified() const noexcept {
    return parameters_->continuous_income && parameters_->income_stratum_adjustment_enabled &&
           !parameters_->income_stratum_expected.empty();
}

std::pair<std::vector<core::Identifier>, std::vector<core::DoubleInterval>>
StaticLinearModel::extended_factors(RuntimeContext &context, bool trended) const {
    auto factors = parameters_->names;
    auto ranges = parameters_->ranges;

    const auto already = [&](const core::Identifier &key) {
        return std::find(parameters_->names.begin(), parameters_->names.end(), key) !=
               parameters_->names.end();
    };

    // Income and physical activity are calibrated alongside the declared factors when the
    // requirements say so AND the FactorsMean tables actually have a column for them. The
    // baseline decides the second half by calling get_expected in a try/catch; here the table is
    // asked directly, because a missing column is a fact about the data, not an exception.
    const bool add_income =
        parameters_->income_enabled &&
        (trended ? parameters_->income_trended : parameters_->adjust_income_to_mean);
    if (add_income && !already(kIncome) && expected().contains(core::Gender::male, kIncome)) {
        factors.push_back(kIncome);
        ranges.push_back(kIncomeAdjustmentRange);
    }

    const bool add_activity = parameters_->physical_activity_enabled &&
                              (trended ? parameters_->physical_activity_trended
                                       : parameters_->adjust_physical_activity_to_mean);
    if (add_activity && !already(kPhysicalActivity) &&
        expected().contains(core::Gender::male, kPhysicalActivity)) {
        factors.push_back(kPhysicalActivity);
        ranges.push_back(context.mapping().contains(kPhysicalActivity) &&
                                 context.mapping().at(kPhysicalActivity).range().has_value()
                             ? *context.mapping().at(kPhysicalActivity).range()
                             : ranges.back());
    }

    return {std::move(factors), std::move(ranges)};
}

void StaticLinearModel::calibrate(RuntimeContext &context, sim::ScenarioJournal &journal,
                                  bool trended) const {
    if (!stratified()) {
        auto [factors, ranges] = extended_factors(context, trended);
        adjust_risk_factors(context, journal, factors, &ranges, trended);
        return;
    }

    // The stratified shape, in the order the baseline runs it and for the reason it gives: income
    // is calibrated against the overall table first, because the strata are ranks *of* income and
    // would otherwise be ranks of an uncalibrated quantity; then the strata are assigned; then
    // every other factor is calibrated one stratum at a time against that stratum's own tables.
    const bool adjust_income =
        parameters_->income_enabled &&
        (trended ? parameters_->income_trended : parameters_->adjust_income_to_mean);
    if (adjust_income) {
        const std::vector<core::Identifier> income_only{kIncome};
        const std::vector<core::DoubleInterval> income_range{kIncomeAdjustmentRange};
        adjust_risk_factors(context, journal, income_only, &income_range, trended);
    }

    assign_adjustment_strata(context.population(), parameters_->adjustment_income_stratum_count);

    // Income and physical activity are excluded here: income was handled above, and physical
    // activity is calibrated in its own pass afterwards because its range comes from the config
    // mapping rather than from the factor list.
    std::vector<core::Identifier> stratum_factors;
    std::vector<core::DoubleInterval> stratum_ranges;
    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        if (parameters_->names[i] == kIncome || parameters_->names[i] == kPhysicalActivity) {
            continue;
        }
        stratum_factors.push_back(parameters_->names[i]);
        stratum_ranges.push_back(parameters_->ranges[i]);
    }

    const bool adjust_activity = parameters_->physical_activity_enabled &&
                                 (trended ? parameters_->physical_activity_trended
                                          : parameters_->adjust_physical_activity_to_mean) &&
                                 expected().contains(core::Gender::male, kPhysicalActivity);
    const std::vector<core::Identifier> activity_only{kPhysicalActivity};
    const std::vector<core::DoubleInterval> activity_range{
        context.mapping().contains(kPhysicalActivity) &&
                context.mapping().at(kPhysicalActivity).range().has_value()
            ? *context.mapping().at(kPhysicalActivity).range()
            : parameters_->ranges.back()};

    const auto strata = std::min(parameters_->adjustment_income_stratum_count,
                                 parameters_->income_stratum_expected.size());
    for (std::size_t k = 0; k < strata; ++k) {
        const AdjustmentScope scope{
            .expected_table = parameters_->income_stratum_expected[k].expected.get(),
            .income_stratum = k};

        if (!stratum_factors.empty()) {
            adjust_risk_factors(context, journal, stratum_factors, &stratum_ranges, trended,
                                scope);
        }
        if (adjust_activity) {
            adjust_risk_factors(context, journal, activity_only, &activity_range, trended, scope);
        }
    }
}

void StaticLinearModel::generate_risk_factors(RuntimeContext &context,
                                              sim::ScenarioJournal &journal) {
    auto &population = context.population();
    auto &random = context.random();

    // The order is load-bearing: it is the order the single random stream is consumed in, and
    // each step reads what the one before it wrote. Sector, then income, because the income
    // regressions have a sector term; then the factors, whose regressions have income terms.
    for (auto &person : population) {
        initialise_sector(person, random);
    }

    for (auto &person : population) {
        initialise_income(context, person, random);
    }

    for (auto &person : population) {
        initialise_factors(context, person, random);
        initialise_physical_activity(context, person, random);
    }

    if (parameters_->adjust_factors_to_mean) {
        calibrate(context, journal, false);
    } else if (stratified()) {
        // The strata still have to exist for the Kevin Hall model's per-quintile weight
        // quantiles and height parameters, even when nothing is calibrated against them.
        assign_adjustment_strata(context.population(),
                                 parameters_->adjustment_income_stratum_count);
    }

    // Continuous income becomes a category only now, so the categories are ranks of the
    // calibrated income rather than of the raw regression output.
    if (parameters_->continuous_income) {
        assign_income_categories(population, parameters_->income_layout);
    }

    for (auto &person : population) {
        initialise_policies(context, person, random, false);
        initialise_trends(context, person);
    }

    if (parameters_->trend_enabled && parameters_->factors_trended) {
        calibrate(context, journal, true);
        if (parameters_->continuous_income) {
            assign_income_categories(population, parameters_->income_layout);
        }
    }
}

void StaticLinearModel::update_risk_factors(RuntimeContext &context,
                                            sim::ScenarioJournal &journal) {
    auto &population = context.population();
    auto &random = context.random();

    const auto policy_start = static_cast<int>(context.inputs().run().policy_start_year);
    const bool intervene = context.scenario().type() == sim::ScenarioType::intervention &&
                           context.time_now() >= policy_start;

    for (auto &person : population) {
        if (!person.is_active()) {
            continue;
        }

        if (person.age == 0) {
            initialise_sector(person, random);
            initialise_income(context, person, random);
            initialise_factors(context, person, random);
            initialise_physical_activity(context, person, random);
        } else {
            update_sector(person, random);
            update_income(context, person, random);
            update_factors(context, person, random);
        }
    }

    if (parameters_->adjust_factors_to_mean) {
        calibrate(context, journal, false);
    } else if (stratified()) {
        assign_adjustment_strata(context.population(),
                                 parameters_->adjustment_income_stratum_count);
    }

    if (parameters_->continuous_income) {
        assign_income_categories(population, parameters_->income_layout);
    }

    for (auto &person : population) {
        if (!person.is_active()) {
            continue;
        }

        if (person.age == 0) {
            initialise_policies(context, person, random, intervene);
            initialise_trends(context, person);
        } else {
            update_policies(context, person, intervene);
            update_trends(context, person);
        }
    }

    if (parameters_->trend_enabled && parameters_->factors_trended) {
        calibrate(context, journal, true);
        if (parameters_->continuous_income) {
            assign_income_categories(population, parameters_->income_layout);
        }
    }

    for (auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        apply_policies(person, intervene);
    }
}

} // namespace hgps::model
