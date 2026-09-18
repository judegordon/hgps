// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/linear_model_evaluator.{h,cpp}, linear_model_eval_options.h,
//         and LinearModelParams from static_linear_model.h.
#pragma once

#include "core/identifier.h"
#include "core/types.h"
#include "model/person.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace hgps::model {

/// @brief One linear model: an intercept, coefficients, and coefficients on logged predictors.
///
/// `std::map`, not `unordered_map` as the baseline has it. Evaluating the model sums these terms,
/// and a floating-point sum over an unordered container's iteration order gives different last
/// bits on a different standard library — the same class of defect as the income CDF (audit
/// B-05), in a place that runs for every person every year. Ordered by identifier, the sum order
/// is stated. Recorded in docs/deviations.md.
struct LinearModelParams {
    double intercept{};
    std::map<core::Identifier, double> coefficients;
    std::map<core::Identifier, double> log_coefficients;

    /// @brief Each coefficient's name resolved to a risk-factor index, in the map's own order.
    ///
    /// Empty until `resolve_predictors` is called, and empty is a valid state: the evaluator falls
    /// back to resolving each name as it goes, which is the same lookup one step later. That is why
    /// a test can build one of these by hand and evaluate it without ceremony.
    ///
    /// Filled, it removes a hash probe and an identifier comparison per coefficient per person per
    /// year — about a third of the `KevinHall_FINCH` profile
    /// ([docs/performance.md](../../../docs/performance.md)).
    std::vector<std::uint32_t> coefficient_indices;
    std::vector<std::uint32_t> log_coefficient_indices;
};

/// @brief Resolves a model's coefficient names to risk-factor indices, once.
///
/// Idempotent, and safe to call again after the coefficients change. Call it when the model is
/// built and never on a hot path: it is the hash probe, hoisted out of the per-person loop.
///
/// It is **not** an optimisation that can change an answer. The index it stores is exactly what
/// `Person::try_risk_factor_value` would have looked up, so a resolved model and an unresolved one
/// evaluate to the same bits; `LinearModelResolutionTest` asserts that over a model with one of
/// every kind of predictor in it.
void resolve_predictors(LinearModelParams &model);

/// @brief Choices the caller makes about how predictors resolve.
struct LinearModelEvalOptions {
    /// @brief When set, the age, age² and age³ terms use this value instead of the person's age.
    ///        From project_requirements.demographics.max_age_for_linear_models.
    std::optional<double> capped_age;

    /// @brief When set, the `gender2` row is 1 for this sex and 0 for the other.
    std::optional<core::Gender> gender2_indicator;

    /// @brief Consulted when a predictor does not resolve from the person — the expected-value
    ///        lookup the adjustable model needs.
    ///
    /// Unlike the baseline this is tried *before* any exception is thrown, not from inside a
    /// catch block: exception-driven control flow on a per-person hot path is both slow and the
    /// shape of code that ends up swallowing real errors.
    std::function<std::optional<double>(const core::Identifier &)> missing_predictor_fallback;
};

/// @brief One predictor's value for a person, honouring `capped_age` and `gender2_indicator`.
/// @throws diag::InternalError if the name resolves to nothing and no fallback supplies it.
double get_linear_predictor_value(const Person &person, const core::Identifier &name,
                                  const LinearModelEvalOptions &options = {});

/// @brief The model's value for a person: intercept plus every term, summed in name order.
///
/// Metadata rows (`StdDev`, `Min`, `Max`, `Lambda`, `Intercept`) are skipped, because the model
/// CSVs carry them alongside the real coefficients.
double evaluate_linear_model(const Person &person, const LinearModelParams &model,
                             const LinearModelEvalOptions &options = {});

} // namespace hgps::model
