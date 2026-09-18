// The intervention policies this model family carries in the person itself: a lifelong policy
// residual, a per-year policy value, and the multiplicative application of it.
//
// These are distinct from the `sim::Scenario` interventions. A scenario intervention is a rule
// about a risk factor written in the config; these are a per-person effect whose coefficients come
// from the model's own policy CSV, and both can be active at once.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: StaticLinearModel::{initialise_policies, update_policies, apply_policies} in
//         src/HealthGPS/static_linear_model.cpp.
#include "static_linear_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <fmt/format.h>

namespace hgps::model {

void StaticLinearModel::initialise_policies(RuntimeContext &context, Person &person,
                                             rng::RandomSource &random, bool intervene) const {
    if (!parameters_->has_active_policies || !person.is_active()) {
        return;
    }

    // Drawn in both scenarios, including the baseline where the values are never used, because
    // the two scenarios share a seed and must consume the stream in step — that is what makes
    // the difference between them attributable to the policy rather than to sampling noise.
    const auto residuals = compute_residuals(random, parameters_->policy_cholesky);
    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        person.risk_factors[derived_keys_.policy_residual[i]] = residuals[i];
    }

    update_policies(context, person, intervene);
}

void StaticLinearModel::update_policies(RuntimeContext &context, Person &person,
                                         bool intervene) const {
    if (!parameters_->has_active_policies) {
        return;
    }

    if (!intervene) {
        for (const auto &key : derived_keys_.policy) {
            person.risk_factors[key] = 0.0;
        }
        return;
    }

    const auto linear = compute_linear_models(context, person, parameters_->policy_models);

    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        const auto &factor = parameters_->names[i];

        const auto residual = person.risk_factors.find(derived_keys_.policy_residual[i]);
        if (residual == person.risk_factors.end()) {
            throw diag::InternalError(
                fmt::format("person {} has no policy residual for '{}'; policies must be "
                            "initialised before they are updated",
                            person.id(), factor.to_string()));
        }

        // The residual is lifelong: it is drawn once and never redrawn, so a person's response to
        // the policy is a fixed characteristic of them rather than a fresh draw each year.
        const double policy = linear[i] + residual->second;
        person.risk_factors[derived_keys_.policy[i]] =
            parameters_->policy_ranges[i].clamp(policy);
    }
}

void StaticLinearModel::apply_policies(Person &person, bool intervene) const {
    if (!parameters_->has_active_policies || !intervene) {
        return;
    }

    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        const auto &factor = parameters_->names[i];

        const auto policy = person.risk_factors.find(derived_keys_.policy[i]);
        const auto value = person.risk_factors.find(factor);
        if (policy == person.risk_factors.end() || value == person.risk_factors.end()) {
            throw diag::InternalError(
                fmt::format("person {} has no policy or no value for '{}' to apply it to",
                            person.id(), factor.to_string()));
        }

        // The policy is a percentage change, which is why it is applied after calibration rather
        // than before: calibrating afterwards would undo it.
        const double adjusted = value->second * (1.0 + policy->second / 100.0);
        value->second = parameters_->ranges[i].clamp(adjusted);
    }
}

} // namespace hgps::model
