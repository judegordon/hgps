// Weight from the quantile curve, and height from the population's weight.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: KevinHallModel::{initialise_weight, get_weight_quantile, compute_mean_weight_for_height,
//         initialise_height, update_height, validate_weight_in_config_range} in
//         src/HealthGPS/kevin_hall_model.cpp.
#include "kevin_hall_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kWeight{"weight"};
const core::Identifier kHeight{"height"};
const core::Identifier kHeightResidual{"height_residual"};
const core::Identifier kEnergyIntake{"energyintake"};
const core::Identifier kPhysicalActivity{"physicalactivity"};

} // namespace

double KevinHallModel::weight_quantile(double epa_quantile,
                                        const std::vector<double> &quantiles) const {
    if (quantiles.empty()) {
        throw diag::InternalError("the weight quantile curve is empty");
    }

    // Where this person's energy-to-activity ratio sits in the reference distribution. A run of
    // equal values is resolved to its midpoint rather than to either end, so a curve with a
    // plateau does not push everyone on it to the same extreme.
    const auto range = std::equal_range(parameters_->epa_quantiles.begin(),
                                        parameters_->epa_quantiles.end(), epa_quantile);
    auto index = static_cast<double>(
        std::distance(parameters_->epa_quantiles.begin(), range.first));
    index += static_cast<double>(std::distance(range.first, range.second)) / 2.0;

    const double percentile = index / static_cast<double>(parameters_->epa_quantiles.size());

    const auto last = quantiles.size() - 1;
    const auto position =
        static_cast<std::size_t>(percentile * static_cast<double>(last));
    return quantiles[std::min(position, last)];
}

void KevinHallModel::initialise_weight(RuntimeContext &context, Person &person) const {
    const auto age = static_cast<int>(person.age);

    const double expected_intake =
        get_expected(context, person.gender, age, kEnergyIntake, std::nullopt, true);
    const double expected_activity =
        get_expected(context, person.gender, age, kPhysicalActivity, std::nullopt, false);
    if (expected_activity <= 0.0) {
        throw diag::InternalError(
            "the expected physical activity level is not positive, so the expected "
            "energy-to-activity ratio is undefined");
    }

    const auto intake = person.risk_factors.find(kEnergyIntake);
    const auto activity = person.risk_factors.find(kPhysicalActivity);
    if (intake == person.risk_factors.end() || activity == person.risk_factors.end()) {
        throw diag::InternalError(fmt::format(
            "person {} has no energy intake or no physical activity, so their weight cannot be "
            "placed on the quantile curve",
            person.id()));
    }
    if (activity->second <= 0.0) {
        throw diag::InternalError(fmt::format(
            "person {} has a physical activity level of {}, so their energy-to-activity ratio is "
            "undefined",
            person.id(), activity->second));
    }

    // Where this person sits relative to the average of their age and sex. That relative position
    // — not the absolute intake — is what the weight curve is indexed by.
    const double epa_quantile =
        (intake->second / activity->second) / (expected_intake / expected_activity);

    const double expected_weight =
        get_expected(context, person.gender, age, kWeight, std::nullopt, true);

    person.risk_factors[kWeight] =
        expected_weight * weight_quantile(epa_quantile, weight_quantiles_for(person));

    validate_weight(context, person, "the weight quantile curve");
}

void KevinHallModel::validate_weight(RuntimeContext &context, const Person &person,
                                      std::string_view phase) const {
    if (!parameters_->weight_range.has_value()) {
        return;
    }

    const auto weight = person.risk_factors.find(kWeight);
    if (weight == person.risk_factors.end()) {
        return;
    }

    const auto &range = *parameters_->weight_range;
    if (weight->second >= range.lower() && weight->second <= range.upper()) {
        return;
    }

    if (weight->second > range.upper()) {
        // Kept, and counted. A body above the configured maximum is implausible but describable,
        // and refusing the run over one person would make the model unusable on a heavy cohort.
        // The baseline prints a line to stdout; a metric survives into the results file.
        context.metrics()["WeightAboveConfiguredMaximum"] += 1.0;
        return;
    }

    // Below the minimum is different: the BMI, the disease models and the weight classification
    // all stop meaning anything, so this is a broken model rather than an extreme person.
    throw diag::InternalError(fmt::format(
        "person {} ({}, age {}) weighs {:.4g} kg after {}, below the configured minimum of "
        "{:.4g} kg for 'Weight'. The energy balance has produced a body the rest of the model "
        "cannot describe; check the model's nutrient and energy coefficients",
        person.id(), person.gender == core::Gender::male ? "male" : "female", person.age,
        weight->second, phase, range.lower()));
}

WeightAdjustmentTable KevinHallModel::mean_weight_power(const Population &population,
                                                         std::optional<unsigned int> age) const {
    struct Moment {
        double sum{};
        int count{};
    };

    Map2d<core::Gender, int, Moment> moments;

    for (const auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        if (age.has_value() && person.age != *age) {
            continue;
        }

        const auto weight = person.risk_factors.find(kWeight);
        if (weight == person.risk_factors.end()) {
            continue;
        }

        const double value = std::pow(weight->second, height_params_for(person).slope);
        const auto key = static_cast<int>(person.age);
        if (!moments.contains(person.gender, key)) {
            moments.emplace(person.gender, key, Moment{});
        }
        auto &moment = moments.at(person.gender, key);
        moment.sum += value;
        ++moment.count;
    }

    WeightAdjustmentTable means;
    for (const auto &[sex, by_age] : moments) {
        for (const auto &[key, moment] : by_age) {
            means.emplace(sex, key,
                          moment.count == 0 ? 0.0
                                            : moment.sum / static_cast<double>(moment.count));
        }
    }

    return means;
}

void KevinHallModel::initialise_height(RuntimeContext &context, Person &person,
                                        double weight_power_mean,
                                        rng::RandomSource &random) const {
    const auto &params = height_params_for(person);

    // Drawn once and kept for life: a person's height relative to others of their age and sex is
    // a characteristic of them, not something that is redrawn each year.
    person.risk_factors[kHeightResidual] = random.next_normal(0.0, params.stddev);

    update_height(context, person, weight_power_mean);
}

void KevinHallModel::update_height(RuntimeContext &context, Person &person,
                                    double weight_power_mean) const {
    if (weight_power_mean <= 0.0) {
        throw diag::InternalError(fmt::format(
            "the mean of weight^slope for {} at age {} is {}, so height cannot be scaled by it",
            person.gender == core::Gender::male ? "male" : "female", person.age,
            weight_power_mean));
    }

    const auto weight = person.risk_factors.find(kWeight);
    const auto residual = person.risk_factors.find(kHeightResidual);
    if (weight == person.risk_factors.end() || residual == person.risk_factors.end()) {
        throw diag::InternalError(
            fmt::format("person {} has no weight or no height residual", person.id()));
    }

    const auto &params = height_params_for(person);
    const double expected = get_expected(context, person.gender, static_cast<int>(person.age),
                                          kHeight, std::nullopt, false);

    // The expected height for this age and sex, scaled by how this person's weight compares with
    // their band's, and by their lifelong residual. The exp(σ²/2) divisor makes the log-normal
    // residual's *mean* one rather than its median, so the scaling is unbiased.
    const double unbiased = std::exp(0.5 * params.stddev * params.stddev);
    person.risk_factors[kHeight] = expected *
                                   (std::pow(weight->second, params.slope) / weight_power_mean) *
                                   (std::exp(residual->second) / unbiased);
}

} // namespace hgps::model
