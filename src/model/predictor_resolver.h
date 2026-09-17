// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/predictor_resolver.h, predictor_resolver.cpp.
#pragma once

#include "core/identifier.h"
#include "person.h"

#include <optional>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief True for rows of a model file that are metadata rather than regression predictors.
///
/// The model CSVs carry `Intercept`, `StdDev`, `Min`, `Max` and `Lambda` rows alongside the real
/// predictors. They are named here, in one place, so that load-time validation can tell "a row
/// that is not a predictor" from "a predictor nobody has heard of" — which is the distinction the
/// earlier rewrite lost when it swallowed the lookup failure and substituted an expected value
/// (audit R-05).
bool is_metadata_predictor(const std::string &name);
bool is_metadata_predictor(const core::Identifier &name);

/// @brief The value of a derived predictor, or nullopt if the name is not one.
///
/// Derived predictors are functions of a person's state that no model file stores: age
/// polynomials (`age`, `age2`, `age3`, …), `log_<name>` and `log_<name><power>`, income
/// polynomials, `income_continuous`, region and ethnicity dummies, sector, and `energyintake`.
std::optional<double> resolve_derived_predictor(const Person &person, const std::string &key);

/// @brief True when the name is the `gender2` regression dummy row.
bool is_gender2_predictor(const std::string &key);

/// @brief 1 when the person's sex is the configured indicator sex, otherwise 0.
double gender2_regression_value(const Person &person, core::Gender indicator_sex);

/// @brief Parses `project_requirements.demographics.gender2`.
/// @throws diag::InternalError for anything but "male" or "female"; the config loader has already
///         rejected other values, so reaching here is a bug.
core::Gender parse_gender2_indicator(const std::string &indicator_label);

/// @brief Whether a name is resolvable for some person: a registered risk factor, a named
///        predictor, a derived predictor, or a metadata row.
///
/// This is what model loading uses to reject an unknown coefficient name before the simulation
/// starts (docs/decisions/0018-no-swallowing-catch-load-time-validation.md). `known_factors` is
/// the set declared in `modelling.risk_factors`.
bool is_resolvable_predictor(const std::string &key,
                             const std::vector<core::Identifier> &known_factors);

/// @brief The registered names closest to `key`, for a "did you mean" in a diagnostic.
std::vector<std::string> nearest_predictor_names(const std::string &key,
                                                 const std::vector<core::Identifier> &known_factors,
                                                 std::size_t limit = 3);

} // namespace hgps::model
