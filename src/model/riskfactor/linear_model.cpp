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

/// One name's answers to the questions the evaluator asks of it, computed from the string.
///
/// `assign_index` is the difference between the two callers, and it is not a detail.
/// `resolve_predictors` runs while the model is being loaded, before any person exists, so a name
/// it uses has often been interned by nothing — and `find` would answer `unknown`, which means
/// "only the string resolver can answer this" and would be frozen in for the life of the model. So
/// it **interns**.
///
/// The per-call fallback must not. Interning writes to a process-wide table, and a model that has
/// not been resolved is evaluated by whoever built it — today only tests, and nothing in a parallel
/// region, but a write on a read path is the shape of a race waiting for a caller. It **finds**,
/// which is exactly what `Person::try_risk_factor_value(name)` did before any of this, including
/// how it treats `unknown`.
LinearModelParams::ResolvedPredictor describe(const core::Identifier &name, bool assign_index) {
    const auto &key = name.to_string();
    LinearModelParams::ResolvedPredictor resolved;
    resolved.index = assign_index ? factor_index().intern(name) : factor_index().find(name);
    resolved.age_power = is_age_predictor(key) ? age_power(key) : 0;
    resolved.gender2 = is_gender2_predictor(key);
    resolved.metadata = is_metadata_predictor(key);
    return resolved;
}

/// The value of a predictor, or nullopt — the non-throwing core of get_linear_predictor_value.
///
/// Everything it needs about the *name* arrives in `resolved`, so nothing here reads a string
/// unless the fallback path does. `options.capped_age` is still consulted per call, because the
/// caller may cap the age for one model and not another.
std::optional<double> try_predictor_value(const Person &person, const core::Identifier &name,
                                          const LinearModelParams::ResolvedPredictor &resolved,
                                          const LinearModelEvalOptions &options) {
    if (options.capped_age.has_value() && resolved.age_power != 0) {
        return std::pow(*options.capped_age, resolved.age_power);
    }

    if (resolved.gender2) {
        // No indicator configured means the male default, as upstream.
        return gender2_regression_value(person, options.gender2_indicator.value_or(
                                                    core::Gender::male));
    }

    if (const auto stored = person.try_risk_factor_value(resolved.index, name)) {
        return stored;
    }

    if (options.missing_predictor_fallback) {
        if (const auto fallback = options.missing_predictor_fallback(name)) {
            return fallback;
        }
    }

    return std::nullopt;
}

/// The stored description for the term at `position`, or a fresh one when the model was never
/// resolved — or was resolved and then had its coefficients changed, which a size mismatch says.
/// Falling back is the only safe reading, and it is the same work one step later.
LinearModelParams::ResolvedPredictor
describe_at(const std::vector<LinearModelParams::ResolvedPredictor> &resolved,
            std::size_t position, std::size_t expected, const core::Identifier &name) {
    return resolved.size() == expected ? resolved[position] : describe(name, false);
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
        std::vector<LinearModelParams::ResolvedPredictor> resolved;
        resolved.reserve(coefficients.size());
        for (const auto &[name, _] : coefficients) {
            resolved.push_back(describe(name, true));
        }
        return resolved;
    };

    model.resolved_coefficients = resolve(model.coefficients);
    model.resolved_log_coefficients = resolve(model.log_coefficients);
}

double get_linear_predictor_value(const Person &person, const core::Identifier &name,
                                  const LinearModelEvalOptions &options) {
    if (const auto value = try_predictor_value(person, name, describe(name, false), options)) {
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
        const auto resolved = describe_at(model.resolved_coefficients, position,
                                          model.coefficients.size(), name);
        ++position;
        if (resolved.metadata) {
            continue;
        }
        const auto value = try_predictor_value(person, name, resolved, options);
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
        const auto resolved = describe_at(model.resolved_log_coefficients, position,
                                          model.log_coefficients.size(), name);
        ++position;
        const auto value = try_predictor_value(person, name, resolved, options);
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
