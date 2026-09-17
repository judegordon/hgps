// The two time trends: the ultra-processed-food trend, which multiplies a factor by a
// person-specific rate raised to the elapsed years, and the income trend, which does the same with
// an exponential decay.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: StaticLinearModel::{initialise_UPF_trends, update_UPF_trends, initialise_income_trends,
//         update_income_trends} in src/HealthGPS/static_linear_model.cpp.
#include "static_linear_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

namespace hgps::model {
namespace {

core::Identifier trend_key(const core::Identifier &factor) {
    return core::Identifier{factor.to_string() + "_trend"};
}

core::Identifier income_trend_key(const core::Identifier &factor) {
    return core::Identifier{factor.to_string() + "_income_trend"};
}

double lookup(const std::map<core::Identifier, double> &table, const core::Identifier &key,
              std::string_view what) {
    const auto found = table.find(key);
    if (found == table.end()) {
        throw diag::InternalError(
            fmt::format("no {} for risk factor '{}'", what, key.to_string()));
    }
    return found->second;
}

} // namespace

void StaticLinearModel::initialise_trends(RuntimeContext &context, Person &person) const {
    if (!parameters_->trend_enabled) {
        return;
    }

    switch (parameters_->trend_type) {
    case TrendType::Null:
        break;
    case TrendType::UpfTrend:
        initialise_upf_trends(context, person);
        break;
    case TrendType::IncomeTrend:
        initialise_income_trends(context, person);
        break;
    }
}

void StaticLinearModel::update_trends(RuntimeContext &context, Person &person) const {
    if (!parameters_->trend_enabled) {
        return;
    }

    switch (parameters_->trend_type) {
    case TrendType::Null:
        break;
    case TrendType::UpfTrend:
        update_upf_trends(context, person);
        break;
    case TrendType::IncomeTrend:
        update_income_trends(context, person);
        break;
    }
}

void StaticLinearModel::initialise_upf_trends(RuntimeContext &context, Person &person) const {
    const auto linear = compute_linear_models(context, person, parameters_->trend_models);

    // A person's trend rate is drawn once and kept: it is a property of them, not of the year.
    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        const auto &factor = parameters_->names[i];
        const double expected =
            lookup(parameters_->expected_trend_boxcox, factor, "expected trend Box-Cox value");
        const double trend = expected * inverse_box_cox(linear[i], parameters_->trend_lambda[i]);
        person.risk_factors[trend_key(factor)] = parameters_->trend_ranges[i].clamp(trend);
    }

    update_upf_trends(context, person);
}

void StaticLinearModel::update_upf_trends(RuntimeContext &context, Person &person) const {
    const int elapsed = context.time_now() - context.start_time();

    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        const auto &factor = parameters_->names[i];

        const auto trend = person.risk_factors.find(trend_key(factor));
        const auto value = person.risk_factors.find(factor);
        if (trend == person.risk_factors.end() || value == person.risk_factors.end()) {
            throw diag::InternalError(
                fmt::format("person {} has no trend or no value for '{}'", person.id(),
                            factor.to_string()));
        }

        // Capped at the factor's own number of trend steps: a trend that runs for ten years does
        // not keep compounding for forty.
        const int steps = std::min(elapsed, get_trend_steps(factor));
        const double trended = value->second * std::pow(trend->second, steps);
        value->second = parameters_->ranges[i].clamp(trended);
    }
}

void StaticLinearModel::initialise_income_trends(RuntimeContext &context, Person &person) const {
    if (parameters_->income_trend_models.empty()) {
        throw diag::InternalError(
            "the income trend is enabled but the model carries no income trend equations");
    }

    const auto linear = compute_linear_models(context, person, parameters_->income_trend_models);

    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        const auto &factor = parameters_->names[i];
        const double expected = lookup(parameters_->expected_income_trend_boxcox, factor,
                                        "expected income trend Box-Cox value");
        const double trend =
            expected * inverse_box_cox(linear[i], parameters_->income_trend_lambda[i]);
        person.risk_factors[income_trend_key(factor)] =
            parameters_->income_trend_ranges[i].clamp(trend);
    }

    update_income_trends(context, person);
}

void StaticLinearModel::update_income_trends(RuntimeContext &context, Person &person) const {
    if (parameters_->income_trend_models.empty()) {
        return;
    }

    const int elapsed = context.time_now() - context.start_time();
    if (elapsed <= 0) {
        // The income trend applies from the second year: at the start year the factors are as
        // measured, and multiplying them by a trend would move them off their own data.
        return;
    }

    for (std::size_t i = 0; i < parameters_->names.size(); ++i) {
        const auto &factor = parameters_->names[i];

        const auto trend = person.risk_factors.find(income_trend_key(factor));
        const auto value = person.risk_factors.find(factor);
        if (trend == person.risk_factors.end() || value == person.risk_factors.end()) {
            throw diag::InternalError(
                fmt::format("person {} has no income trend or no value for '{}'", person.id(),
                            factor.to_string()));
        }

        const auto steps_found = parameters_->income_trend_steps.find(factor);
        const int steps =
            std::min(elapsed, steps_found == parameters_->income_trend_steps.end()
                                  ? 0
                                  : steps_found->second);
        const double decay =
            lookup(parameters_->income_trend_decay_factors, factor, "income trend decay factor");

        const double trended = value->second * trend->second * std::exp(decay * steps);
        value->second = parameters_->ranges[i].clamp(trended);
    }
}

} // namespace hgps::model
