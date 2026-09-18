#include "linear_model.h"

#include "core/chars.h"
#include "diagnostics/internal_error.h"
#include "model/factor_values.h"
#include "model/predictor_resolver.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include <fmt/format.h>

namespace hgps::model {
namespace {

/// The log terms floor their argument, because log(0) is not a number a risk factor can carry.
constexpr double kLogFloor = 1e-10;

bool is_age_predictor(const std::string &key) {
    return key.starts_with("age") || key.starts_with("Age");
}

/// "age" and "age1" are the first power, "age2" the second, and so on.
int age_power(const std::string &key) {
    if (key.size() <= 3) {
        return 1;
    }

    int power = 0;
    for (std::size_t i = 3; i < key.size(); ++i) {
        if (!core::chars::is_digit(key[i])) {
            return 1;
        }
        power = power * 10 + (key[i] - '0');
    }

    return power == 0 ? 1 : power;
}

/// The index a caller passes when it has not resolved the name: "look it up yourself".
constexpr std::uint32_t kUnresolved = 0xFFFFFFFEU;

/// The value of a predictor, or nullopt — the non-throwing core of get_linear_predictor_value.
///
/// `index` is the name's risk-factor index when the model has been through `resolve_predictors`,
/// and `kUnresolved` when it has not. The only thing it changes is whether the hash probe happens
/// here or one call down; nothing else in this function depends on it.
std::optional<double> try_predictor_value(const Person &person, const core::Identifier &name,
                                          std::uint32_t index,
                                          const LinearModelEvalOptions &options) {
    const auto &key = name.to_string();

    if (options.capped_age.has_value() && is_age_predictor(key)) {
        return std::pow(*options.capped_age, age_power(key));
    }

    if (is_gender2_predictor(key)) {
        // No indicator configured means the male default, as upstream.
        return gender2_regression_value(person, options.gender2_indicator.value_or(
                                                    core::Gender::male));
    }

    const auto stored = index == kUnresolved ? person.try_risk_factor_value(name)
                                             : person.try_risk_factor_value(index, name);
    if (stored) {
        return stored;
    }

    if (options.missing_predictor_fallback) {
        if (const auto fallback = options.missing_predictor_fallback(name)) {
            return fallback;
        }
    }

    return std::nullopt;
}

/// The index for the term at `position`, or `kUnresolved` when the model was never resolved.
std::uint32_t index_at(const std::vector<std::uint32_t> &indices, std::size_t position,
                       std::size_t expected) {
    // A size mismatch means the coefficients changed after `resolve_predictors` ran, so the stored
    // indices are not this model's any more. Falling back rather than trusting them is the only
    // safe reading, and it is the same lookup one step later.
    return indices.size() == expected ? indices[position] : kUnresolved;
}

} // namespace

void resolve_predictors(LinearModelParams &model) {
    // Builds the nineteen derived-predictor names' indices before anything else asks for one. A
    // name this table has not interned resolves only through the string path, so resolving before
    // it exists would freeze `age` into the slow branch for the life of the model — the same window
    // docs/performance.md records `Person::try_risk_factor_value` falling into once already.
    intern_derived_predictors();

    // `intern`, not `find`. A coefficient name that nothing has interned *yet* is not the same
    // thing as one that will never be stored: `find` answers `unknown` for both, and `unknown`
    // means "only the string resolver can answer this", which would be frozen into the model for
    // the life of the run. A model is resolved when it is loaded, before any person exists, so the
    // risk-factor names it uses have often not been interned by anything at that point.
    //
    // Interning is safe as well as correct. The table is append-only and process-wide, so a name
    // that turns out never to be stored costs one entry and one extra pass over the nineteen-entry
    // dispatcher before the same fallback answers it — the identical value, one scan later.
    //
    // The unresolved path passed a whole `KevinHall_FINCH` run byte for byte, because something
    // else happened to have interned every name it uses first. `LinearModelResolution
    // .ItIsIdempotentAndSurvivesTheCoefficientsChanging` is what found it, by resolving a model
    // before building the person it is evaluated against.
    const auto resolve = [](const std::map<core::Identifier, double> &coefficients) {
        std::vector<std::uint32_t> indices;
        indices.reserve(coefficients.size());
        for (const auto &[name, _] : coefficients) {
            indices.push_back(factor_index().intern(name));
        }
        return indices;
    };

    model.coefficient_indices = resolve(model.coefficients);
    model.log_coefficient_indices = resolve(model.log_coefficients);
}

double get_linear_predictor_value(const Person &person, const core::Identifier &name,
                                  const LinearModelEvalOptions &options) {
    if (const auto value = try_predictor_value(person, name, kUnresolved, options)) {
        return *value;
    }

    // Every coefficient name is checked against the registered factor set when its model file is
    // loaded (ADR 0018), so an unresolvable name here is a bug, not a user mistake.
    throw diag::InternalError(
        fmt::format("linear model predictor '{}' does not resolve for this person, and no "
                    "fallback supplied a value",
                    name.to_string()));
}

double evaluate_linear_model(const Person &person, const LinearModelParams &model,
                             const LinearModelEvalOptions &options) {
    double linear = model.intercept;

    // Both loops walk an ordered map, so the summation order is the coefficients' name order and
    // does not vary with the standard library. The index vectors are in that same order, which is
    // why they are positional rather than a second map: a map keyed by name would put the lookup
    // back.
    std::size_t position = 0;
    for (const auto &[name, coefficient] : model.coefficients) {
        const auto index = index_at(model.coefficient_indices, position, model.coefficients.size());
        ++position;
        if (is_metadata_predictor(name)) {
            continue;
        }
        const auto value = try_predictor_value(person, name, index, options);
        if (!value.has_value()) {
            // Every coefficient name is checked against the registered factor set when its model
            // file is loaded (ADR 0018), so an unresolvable name here is a bug, not a user mistake.
            throw diag::InternalError(
                fmt::format("linear model predictor '{}' does not resolve for this person, and no "
                            "fallback supplied a value",
                            name.to_string()));
        }
        linear += coefficient * *value;
    }

    position = 0;
    for (const auto &[name, coefficient] : model.log_coefficients) {
        const auto index =
            index_at(model.log_coefficient_indices, position, model.log_coefficients.size());
        ++position;
        const auto value = try_predictor_value(person, name, index, options);
        if (!value.has_value()) {
            throw diag::InternalError(
                fmt::format("linear model log predictor '{}' does not resolve for this person",
                            name.to_string()));
        }
        linear += coefficient * std::log(std::max(*value, kLogFloor));
    }

    return linear;
}

} // namespace hgps::model
