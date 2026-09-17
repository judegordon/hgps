// The risk factors themselves: correlated residuals, the linear models, the two-stage logistic
// first step and the inverse Box-Cox second step.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: StaticLinearModel::{compute_residuals, compute_linear_models, calculate_zero_probability,
//         initialise_factors, update_factors, inverse_box_cox} in
//         src/HealthGPS/static_linear_model.cpp.
#include "static_linear_model.h"

#include "core/string_util.h"
#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kEnergyIntake{"energyintake"};

/// A residual is stored under the factor's name with this suffix, so that next year's blend can
/// find it. Same spelling as the baseline, because these keys reach the output.
core::Identifier residual_key(const core::Identifier &factor) {
    return core::Identifier{factor.to_string() + "_residual"};
}

} // namespace

double StaticLinearModel::inverse_box_cox(double factor, double lambda) {
    // lambda == 0 is the log case, whose inverse is the exponential. The comparison is against a
    // tolerance rather than exact zero because the value is read from a CSV.
    if (std::abs(lambda) < 1e-10) {
        const double result = std::exp(factor);
        return std::isfinite(result) ? result : 0.0;
    }

    const double base = lambda * factor + 1.0;
    if (base <= 0.0) {
        // Outside the transform's domain. A residual far enough into the tail gets here, and the
        // baseline's answer — and this one — is zero rather than a NaN that would then propagate
        // through the calibration into every other person's value.
        return 0.0;
    }

    const double result = std::pow(base, 1.0 / lambda);
    if (!std::isfinite(result)) {
        return 0.0;
    }

    return std::max(0.0, result);
}

LinearModelEvalOptions StaticLinearModel::eval_options(const Person & /*person*/) const {
    LinearModelEvalOptions options;
    options.gender2_indicator = parameters_->gender2_indicator;
    return options;
}

LinearModelEvalOptions StaticLinearModel::capped_eval_options(const Person &person) const {
    auto options = eval_options(person);

    // `min(age, cap)`, not the cap. The regressions are fitted on a population whose oldest
    // members are lumped together at the top age, so ages above the cap use the cap and ages
    // below it use themselves. Reading it as the cap alone would give every person in every model
    // the same age — a couple of hundred units in the income regression, and enough to push the
    // whole population below the bottom of the physical-activity range.
    //
    // The cap applies to the **risk factor, policy and trend** models only, and not to the income,
    // physical-activity or logistic regressions. That is the baseline's split, and it is not
    // arbitrary: those three are fitted over the whole age range while the factor models are
    // fitted with the top ages pooled. Applying it everywhere raises the mean physical activity of
    // the FINCH cohort by about 0.1%, which the equivalence harness sees.
    if (parameters_->max_age_for_linear_models.has_value() &&
        *parameters_->max_age_for_linear_models > 0) {
        options.capped_age = std::min(static_cast<double>(person.age),
                                      static_cast<double>(*parameters_->max_age_for_linear_models));
    }

    return options;
}

std::vector<double> StaticLinearModel::compute_residuals(rng::RandomSource &random,
                                                          const core::Matrix &cholesky) const {
    // Independent standard normals in factor order, then correlated by the Cholesky factor. The
    // draw order is the factor order, which is the correlation matrix's column order, which is
    // what makes the residual for factor i the one the matrix says it is.
    std::vector<double> independent(parameters_->names.size());
    for (auto &value : independent) {
        value = random.next_normal();
    }

    return cholesky.multiply(independent);
}

std::vector<double>
StaticLinearModel::compute_linear_models(RuntimeContext &context, const Person &person,
                                         const std::vector<LinearModelParams> &models) const {
    auto options = capped_eval_options(person);

    // A coefficient may name a factor this person does not carry yet — energy intake while the
    // nutrients are still being generated, most often. The baseline substitutes the expected
    // value for that age and sex, and does it from inside a catch block; here it is a fallback
    // the evaluator consults before throwing, so the hot path has no exceptions in it.
    options.missing_predictor_fallback =
        [this, &context, &person](const core::Identifier &name) -> std::optional<double> {
        const auto &key = name.to_string();

        // log_energy_intake and log_energyintake are the same row under two spellings; both mean
        // the log of the expected energy intake.
        if (core::case_insensitive::equals(key, "log_energy_intake") ||
            core::case_insensitive::equals(key, "log_energyintake")) {
            if (!expected().contains(person.gender, kEnergyIntake)) {
                return std::nullopt;
            }
            const double value = expected_from(expected(), context, person.gender,
                                                static_cast<int>(person.age), kEnergyIntake,
                                                std::nullopt, false);
            return std::log(std::max(value, 1e-10));
        }

        if (!expected().contains(person.gender, name)) {
            return std::nullopt;
        }
        return expected_from(expected(), context, person.gender, static_cast<int>(person.age),
                             name, std::nullopt, false);
    };

    std::vector<double> linear;
    linear.reserve(parameters_->names.size());
    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        linear.push_back(evaluate_linear_model(person, models[i], options));
    }

    return linear;
}

double StaticLinearModel::zero_probability(const Person &person, std::size_t factor_index) const {
    const double linear =
        evaluate_linear_model(person, parameters_->logistic_models[factor_index], eval_options(person));

    // The logistic function, written so that a large positive linear term does not overflow: for
    // x >= 0 the exponential is of -x and so is in (0, 1]. The baseline writes 1/(1+exp(-x)),
    // which overflows to infinity for x below about -709 and then returns 0 anyway — the same
    // answer, by accident rather than by construction, and with a floating-point overflow on the
    // way. Tested by StaticLinearTwoStageTest.
    if (linear >= 0.0) {
        return 1.0 / (1.0 + std::exp(-linear));
    }
    const double exponential = std::exp(linear);
    return exponential / (1.0 + exponential);
}

void StaticLinearModel::initialise_factors(RuntimeContext &context, Person &person,
                                            rng::RandomSource &random) const {
    if (!person.is_active()) {
        return;
    }

    const auto residuals = compute_residuals(random, parameters_->cholesky);
    const auto linear = compute_linear_models(context, person, parameters_->models);

    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        const auto &factor = parameters_->names[i];
        person.risk_factors[residual_key(factor)] = residuals[i];

        const double expected_value = get_expected(context, person.gender,
                                                    static_cast<int>(person.age), factor,
                                                    parameters_->ranges[i], false);

        // Stage one, where the factor has a logistic model: is this person a zero at all? A
        // factor with no logistic coefficients skips it and is Box-Cox only — that is what an
        // empty model means, not "a model that always says no".
        if (!parameters_->logistic_models[i].coefficients.empty()) {
            if (random.next_double() < zero_probability(person, i)) {
                person.risk_factors[factor] = 0.0;
                continue;
            }
        }

        // Stage two: the linear prediction plus the correlated residual, through the inverse
        // Box-Cox transform, scaled by the expected value for this age and sex.
        double value = linear[i] + residuals[i] * parameters_->stddev[i];
        value = expected_value * inverse_box_cox(value, parameters_->lambda[i]);
        person.risk_factors[factor] = parameters_->ranges[i].clamp(value);
    }
}

void StaticLinearModel::update_factors(RuntimeContext &context, Person &person,
                                        rng::RandomSource &random) const {
    const auto fresh = compute_residuals(random, parameters_->cholesky);
    const auto linear = compute_linear_models(context, person, parameters_->models);

    const double speed = parameters_->info_speed;
    const double carried = std::sqrt(1.0 - speed * speed);

    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        const auto &factor = parameters_->names[i];
        const double expected_value = get_expected(context, person.gender,
                                                    static_cast<int>(person.age), factor,
                                                    parameters_->ranges[i], false);

        // The residual is an AR(1): `info_speed` of it is redrawn and the rest carried over, with
        // the coefficient chosen so the variance is preserved. That is what makes a person's
        // factors correlated with their own past rather than resampled every year.
        const auto key = residual_key(factor);
        const auto previous = person.risk_factors.find(key);
        if (previous == person.risk_factors.end()) {
            throw diag::InternalError(
                fmt::format("person {} has no '{}' to update; the static model must have "
                            "generated them before the dynamic model updates them",
                            person.id(), key.to_string()));
        }

        const double residual = fresh[i] * speed + carried * previous->second;
        previous->second = residual;

        if (!parameters_->logistic_models[i].coefficients.empty()) {
            double probability = zero_probability(person, i);

            // The zero/non-zero state has to persist between years or a person would flicker in
            // and out of eating a food group. The baseline averages this year's probability with
            // last year's outcome — 1 if they were a zero, 0 if they were not — which gives a
            // one-year memory. Carried over unchanged, including the name the baseline gives it.
            const auto current = person.risk_factors.find(factor);
            const bool was_zero = current != person.risk_factors.end() && current->second == 0.0;
            probability = (probability + (was_zero ? 1.0 : 0.0)) / 2.0;

            if (random.next_double() < probability) {
                person.risk_factors[factor] = 0.0;
                continue;
            }
        }

        double value = linear[i] + residual * parameters_->stddev[i];
        value = expected_value * inverse_box_cox(value, parameters_->lambda[i]);
        person.risk_factors[factor] = parameters_->ranges[i].clamp(value);
    }
}

} // namespace hgps::model
