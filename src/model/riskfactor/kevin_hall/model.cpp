// The Kevin Hall model's lifecycle and its derived expected values.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: KevinHallModel::{generate_risk_factors, update_risk_factors, update_newborns,
//         update_non_newborns, get_expected, compute_weight_adjustments,
//         receive_weight_adjustments} in src/HealthGPS/kevin_hall_model.cpp.
#include "kevin_hall_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"
#include "sim/scenario.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kWeight{"weight"};
const core::Identifier kHeight{"height"};
const core::Identifier kEnergyIntake{"energyintake"};
const core::Identifier kPhysicalActivity{"physicalactivity"};

} // namespace

KevinHallModel::KevinHallModel(std::shared_ptr<const SexAgeFactorTable> expected,
                                std::shared_ptr<const std::map<core::Identifier, double>> trend,
                                std::shared_ptr<const std::map<core::Identifier, int>> trend_steps,
                                std::shared_ptr<const KevinHallParameters> parameters)
    : AdjustableRiskFactorModel{std::move(expected), std::move(trend), std::move(trend_steps),
                                TrendType::Null},
      parameters_{std::move(parameters)} {
    if (!parameters_) {
        throw diag::InternalError("the Kevin Hall model needs its parameters");
    }
    if (parameters_->energy_equation.empty()) {
        throw diag::InternalError("the Kevin Hall model has no nutrients");
    }

    // Every name in the two hot loops resolved to an index, once. `intern` rather than `find`,
    // because a nutrient or a food group may not have been seen yet at this point — it is a name
    // this model is about to start writing, and interning it here is what makes the index exist.
    // The derived-predictor names are interned first for the reason `resolve_predictors` gives.
    intern_derived_predictors();
    for (const auto &[food, nutrients] : parameters_->nutrient_equations) {
        ResolvedFood resolved{.food = factor_index().intern(food), .nutrients = {}};
        resolved.nutrients.reserve(nutrients.size());
        for (const auto &[nutrient, coefficient] : nutrients) {
            resolved.nutrients.emplace_back(factor_index().intern(nutrient), coefficient);
        }
        resolved_foods_.push_back(std::move(resolved));
    }
    resolved_energy_.reserve(parameters_->energy_equation.size());
    for (const auto &[nutrient, coefficient] : parameters_->energy_equation) {
        resolved_energy_.emplace_back(factor_index().intern(nutrient), coefficient);
    }
    if (parameters_->nutrient_equations.empty()) {
        throw diag::InternalError("the Kevin Hall model has no food groups");
    }
    if (parameters_->epa_quantiles.empty()) {
        throw diag::InternalError("the Kevin Hall model has no energy/activity quantiles");
    }

    for (const auto sex : {core::Gender::male, core::Gender::female}) {
        const auto quantiles = parameters_->weight_quantiles.find(sex);
        if (quantiles == parameters_->weight_quantiles.end() || quantiles->second.empty()) {
            throw diag::InternalError(
                fmt::format("the Kevin Hall model has no {} weight quantiles",
                            sex == core::Gender::male ? "male" : "female"));
        }
        for (const auto &curve : quantiles->second) {
            if (curve.empty()) {
                throw diag::InternalError("a Kevin Hall weight quantile curve is empty");
            }
        }

        const auto height = parameters_->height_params.find(sex);
        if (height == parameters_->height_params.end() || height->second.empty()) {
            throw diag::InternalError(
                fmt::format("the Kevin Hall model has no {} height parameters",
                            sex == core::Gender::male ? "male" : "female"));
        }
    }
}

const HeightModelParams &KevinHallModel::height_params_for(const Person &person) const {
    const auto found = parameters_->height_params.find(person.gender);
    if (found == parameters_->height_params.end() || found->second.empty()) {
        throw diag::InternalError("no height parameters for this person's sex");
    }

    // A single row broadcasts to every stratum, which is how the packs without quintile files are
    // written; otherwise the person's stratum picks the row, clamped in case the pack has fewer
    // rows than the config has strata.
    if (!person.has_income_adjustment_stratum) {
        return found->second.front();
    }
    return found->second[std::min(person.income_adjustment_stratum, found->second.size() - 1)];
}

const std::vector<double> &KevinHallModel::weight_quantiles_for(const Person &person) const {
    const auto found = parameters_->weight_quantiles.find(person.gender);
    if (found == parameters_->weight_quantiles.end() || found->second.empty()) {
        throw diag::InternalError("no weight quantiles for this person's sex");
    }

    if (!person.has_income_adjustment_stratum) {
        return found->second.front();
    }
    return found->second[std::min(person.income_adjustment_stratum, found->second.size() - 1)];
}

double KevinHallModel::get_expected(RuntimeContext &context, core::Gender sex, int age,
                                     const core::Identifier &factor,
                                     std::optional<core::DoubleInterval> range,
                                     bool apply_trend) const {
    // A nutrient's expected intake is not in the FactorsMean tables: it is derived from the
    // expected *food* intakes, which are, through the food-to-nutrient matrix.
    if (parameters_->energy_equation.contains(factor)) {
        double intake = 0.0;
        for (const auto &[food, nutrients] : parameters_->nutrient_equations) {
            const auto coefficient = nutrients.find(factor);
            if (coefficient == nutrients.end()) {
                continue;
            }
            intake += get_expected(context, sex, age, food, std::nullopt, apply_trend) *
                      coefficient->second;
        }
        return intake;
    }

    if (factor == kEnergyIntake) {
        if (!apply_trend) {
            return AdjustableRiskFactorModel::get_expected(context, sex, age, kEnergyIntake,
                                                            std::nullopt, false);
        }

        // With a trend, energy intake is rebuilt from the trended nutrients rather than trended
        // itself, so the two stay consistent.
        double energy = 0.0;
        for (const auto &[nutrient, coefficient] : parameters_->energy_equation) {
            energy += get_expected(context, sex, age, nutrient, std::nullopt, true) * coefficient;
        }
        return energy;
    }

    if (factor == kWeight) {
        if (!apply_trend) {
            return AdjustableRiskFactorModel::get_expected(context, sex, age, kWeight,
                                                            std::nullopt, false);
        }

        if (age < kAdultAge) {
            // A child's trended weight scales with the trended energy intake to the power 0.45,
            // which is the allometric relation the model uses in place of the adult balance.
            const double weight = AdjustableRiskFactorModel::get_expected(
                context, sex, age, kWeight, std::nullopt, false);
            const double plain =
                get_expected(context, sex, age, kEnergyIntake, std::nullopt, false);
            const double trended =
                get_expected(context, sex, age, kEnergyIntake, std::nullopt, true);
            if (plain <= 0.0) {
                throw diag::InternalError(
                    "the expected energy intake is not positive, so a child's trended weight "
                    "cannot be scaled by it");
            }
            return weight * std::pow(trended / plain, 0.45);
        }

        // The adult regression of weight on energy intake, height and age. The energy coefficient
        // is 1/(9.99 × PAL) rather than a constant, so that a population with a different
        // physical activity level gets a different expected weight for the same intake.
        const double activity = get_expected(context, sex, age, kPhysicalActivity, std::nullopt,
                                              false);
        if (activity <= 0.0) {
            throw diag::InternalError(
                "the expected physical activity level is not positive, so the expected adult "
                "weight cannot be computed");
        }

        double weight = 16.1161;
        weight += (1.0 / (9.99 * activity)) *
                  get_expected(context, sex, age, kEnergyIntake, std::nullopt, true);
        weight -= 0.6256 * get_expected(context, sex, age, kHeight, std::nullopt, false);
        weight += 0.4925 * age;
        weight -= sex == core::Gender::male ? 16.6166 : 0.0;
        return weight;
    }

    return AdjustableRiskFactorModel::get_expected(context, sex, age, factor, range, apply_trend);
}

WeightAdjustmentTable
KevinHallModel::compute_weight_adjustments(RuntimeContext &context,
                                            std::optional<unsigned int> age) const {
    const auto age_count = static_cast<std::size_t>(context.age_range().upper()) + 1;

    struct Moment {
        double sum{};
        int count{};
    };

    Map2d<core::Gender, int, Moment> moments;
    for (const auto sex : {core::Gender::male, core::Gender::female}) {
        for (std::size_t a = 0; a < age_count; ++a) {
            moments.emplace(sex, static_cast<int>(a), Moment{});
        }
    }

    for (const auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }
        if (age.has_value() && person.age != *age) {
            continue;
        }
        if (static_cast<std::size_t>(person.age) >= age_count) {
            continue;
        }

        const auto weight = person.risk_factors.find(kWeight);
        if (weight == person.risk_factors.end()) {
            continue;
        }

        auto &moment = moments.at(person.gender, static_cast<int>(person.age));
        moment.sum += weight->second;
        ++moment.count;
    }

    WeightAdjustmentTable adjustments;
    for (const auto sex : {core::Gender::male, core::Gender::female}) {
        for (std::size_t a = 0; a < age_count; ++a) {
            const auto &moment = moments.at(sex, static_cast<int>(a));
            if (moment.count == 0) {
                // Nobody of this age and sex, so there is nothing to calibrate and the delta is
                // zero rather than a NaN that would then be added to somebody's weight.
                adjustments.emplace(sex, static_cast<int>(a), 0.0);
                continue;
            }

            const double expected = get_expected(context, sex, static_cast<int>(a), kWeight,
                                                  std::nullopt, true);
            adjustments.emplace(sex, static_cast<int>(a),
                                expected - moment.sum / static_cast<double>(moment.count));
        }
    }

    return adjustments;
}

WeightAdjustmentTable KevinHallModel::weight_adjustments(RuntimeContext &context,
                                                          sim::ScenarioJournal &journal,
                                                          std::optional<unsigned int> age) const {
    const auto age_count = static_cast<std::size_t>(context.age_range().upper()) + 1;

    if (context.scenario().type() == sim::ScenarioType::baseline) {
        auto adjustments = compute_weight_adjustments(context, age);

        // Recorded in the same journal as the risk-factor calibration, as one more table in the
        // year's sequence, so the intervention replays both in the order the baseline produced
        // them (ADR 0009).
        sim::AdjustmentTable recorded;
        for (const auto sex : {core::Gender::male, core::Gender::female}) {
            std::vector<double> by_age(age_count, 0.0);
            for (std::size_t a = 0; a < age_count; ++a) {
                by_age[a] = adjustments.at(sex, static_cast<int>(a));
            }
            recorded.emplace(sex, kWeight, std::move(by_age));
        }
        journal.push_adjustment(context.current_run(), context.time_now(), std::move(recorded));

        return adjustments;
    }

    const auto &replayed = journal.pop_adjustment(context.current_run(), context.time_now());

    WeightAdjustmentTable adjustments;
    for (const auto sex : {core::Gender::male, core::Gender::female}) {
        if (!replayed.contains(sex, kWeight)) {
            throw diag::InternalError(
                "the baseline scenario recorded no weight adjustment for this sex; the two "
                "scenarios' models disagree");
        }
        const auto &by_age = replayed.at(sex, kWeight);
        for (std::size_t a = 0; a < age_count && a < by_age.size(); ++a) {
            adjustments.emplace(sex, static_cast<int>(a), by_age[a]);
        }
    }

    return adjustments;
}

void KevinHallModel::generate_risk_factors(RuntimeContext &context,
                                            sim::ScenarioJournal &journal) {
    auto &population = context.population();

    for (auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        initialise_nutrient_intakes(person);
        initialise_energy_intake(person);
        initialise_weight(context, person);
    }

    // Calibrate weight before height, because height is modelled on the population's weight.
    const std::vector<core::Identifier> weight_only{kWeight};
    adjust_risk_factors(context, journal, weight_only, nullptr, true);

    for (const auto &person : population) {
        if (person.is_active()) {
            validate_weight(context, person, "generate");
        }
    }

    const auto power_means = mean_weight_power(population, std::nullopt);

    for (auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        initialise_height(context, person,
                          power_means.at(person.gender, static_cast<int>(person.age)),
                          context.random());
        initialise_state(context, person);
        compute_bmi(person);
    }
}

void KevinHallModel::update_risk_factors(RuntimeContext &context,
                                          sim::ScenarioJournal &journal) {
    update_newborns(context, journal);
    update_others(context, journal);

    for (auto &person : context.population()) {
        if (person.is_active()) {
            compute_bmi(person);
        }
    }
}

void KevinHallModel::update_newborns(RuntimeContext &context,
                                      sim::ScenarioJournal &journal) const {
    auto &population = context.population();

    for (auto &person : population) {
        if (!person.is_active() || person.age != 0) {
            continue;
        }
        initialise_nutrient_intakes(person);
        initialise_energy_intake(person);
        initialise_weight(context, person);
    }

    // Newborns are calibrated on their own, because they were generated this step and the rest of
    // the cohort was moved by the energy balance; a single pass would mix the two.
    const auto adjustments = weight_adjustments(context, journal, 0U);

    for (auto &person : population) {
        if (!person.is_active() || person.age != 0) {
            continue;
        }
        person.risk_factors.at(kWeight) += adjustments.at(person.gender, 0);
        validate_weight(context, person, "newborn calibration");
    }

    const auto power_means = mean_weight_power(population, 0U);

    for (auto &person : population) {
        if (!person.is_active() || person.age != 0) {
            continue;
        }
        initialise_height(context, person, power_means.at(person.gender, 0), context.random());
        initialise_state(context, person);
    }
}

void KevinHallModel::update_others(RuntimeContext &context,
                                    sim::ScenarioJournal &journal) const {
    auto &population = context.population();

    for (auto &person : population) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }
        update_nutrient_intakes(person);
        update_energy_intake(person);
    }

    for (auto &person : population) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        if (person.age < static_cast<unsigned int>(kAdultAge)) {
            // Growth dominates energy balance in childhood, so a child's weight comes from the
            // quantile curve each year rather than from the previous year's body composition.
            initialise_weight(context, person);
        } else {
            run_energy_balance(context, person);
        }
    }

    const auto adjustments = weight_adjustments(context, journal, std::nullopt);

    for (auto &person : population) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        const double adjustment =
            adjustments.contains(person.gender, static_cast<int>(person.age))
                ? adjustments.at(person.gender, static_cast<int>(person.age))
                : 0.0;

        if (person.age < static_cast<unsigned int>(kAdultAge)) {
            initialise_state(context, person, adjustment);
        } else {
            adjust_weight(context, person, adjustment);
        }
    }

    const auto power_means = mean_weight_power(population, std::nullopt);

    for (auto &person : population) {
        if (!person.is_active() || person.age == 0 ||
            person.age >= static_cast<unsigned int>(kAdultAge)) {
            continue;
        }
        // Only children's height is updated: an adult's is fixed once they stop growing.
        update_height(context, person,
                      power_means.at(person.gender, static_cast<int>(person.age)));
    }
}

} // namespace hgps::model
