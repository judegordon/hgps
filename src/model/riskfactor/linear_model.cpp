#include "linear_model.h"

#include "core/chars.h"
#include "diagnostics/internal_error.h"
#include "model/predictor_resolver.h"

#include <cmath>

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

/// The value of a predictor, or nullopt — the non-throwing core of get_linear_predictor_value.
std::optional<double> try_predictor_value(const Person &person, const core::Identifier &name,
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

    if (const auto value = person.try_risk_factor_value(name)) {
        return value;
    }

    if (options.missing_predictor_fallback) {
        if (const auto fallback = options.missing_predictor_fallback(name)) {
            return fallback;
        }
    }

    return std::nullopt;
}

} // namespace

double get_linear_predictor_value(const Person &person, const core::Identifier &name,
                                  const LinearModelEvalOptions &options) {
    if (const auto value = try_predictor_value(person, name, options)) {
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
    // does not vary with the standard library.
    for (const auto &[name, coefficient] : model.coefficients) {
        if (is_metadata_predictor(name)) {
            continue;
        }
        linear += coefficient * get_linear_predictor_value(person, name, options);
    }

    for (const auto &[name, coefficient] : model.log_coefficients) {
        const auto value = try_predictor_value(person, name, options);
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
