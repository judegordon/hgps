#include "staticlinear_trend_loader.h"

#include "json_access.h"

#include <fmt/core.h>

#include <string>

namespace {

hgps::LinearModelParams load_trend_model_from_json(const nlohmann::json &trend_json,
                                                   hgps::core::Diagnostics &diagnostics,
                                                   bool &ok,
                                                   std::string_view source_path,
                                                   std::string_view field_path) {
    hgps::LinearModelParams model;

    get_to(trend_json, "Intercept", model.intercept, diagnostics, ok, source_path, field_path);
    get_to(trend_json, "Coefficients", model.coefficients, diagnostics, ok, source_path,
           field_path);
    get_to(trend_json, "LogCoefficients", model.log_coefficients, diagnostics, ok, source_path,
           field_path);

    return model;
}

} // namespace

namespace hgps::input {

std::optional<hgps::TrendType>
resolve_staticlinear_trend_type(const Configuration &config,
                                hgps::core::Diagnostics &diagnostics,
                                std::string_view source_path,
                                std::string_view field_path) {
    const auto &trend = config.project_requirements.trend;

    if (!trend.enabled) {
        return hgps::TrendType::Null;
    }

    if (trend.type == "null") {
        return hgps::TrendType::Null;
    }

    if (trend.type == "trend" || trend.type == "upf_trend" || trend.type == "UPFTrend") {
        return hgps::TrendType::UPFTrend;
    }

    if (trend.type == "income_trend") {
        return hgps::TrendType::IncomeTrend;
    }

    diagnostics.error(hgps::core::DiagnosticCode::invalid_enum_value,
                      {.source_path = std::string{source_path},
                       .field_path = join_field_path(field_path, "trend.type")},
                      fmt::format("Unsupported trend type '{}'", trend.type));
    return std::nullopt;
}

StaticLinearTrendData initialise_staticlinear_trend_data(hgps::TrendType /*trend_type*/) {
    StaticLinearTrendData trend_data;

    trend_data.models = std::make_unique<std::vector<hgps::LinearModelParams>>();
    trend_data.ranges = std::make_unique<std::vector<hgps::core::DoubleInterval>>();
    trend_data.lambda = std::make_unique<std::vector<double>>();
    trend_data.expected =
        std::make_unique<std::unordered_map<hgps::core::Identifier, double>>();
    trend_data.expected_boxcox =
        std::make_unique<std::unordered_map<hgps::core::Identifier, double>>();
    trend_data.steps = std::make_unique<std::unordered_map<hgps::core::Identifier, int>>();

    trend_data.expected_income =
        std::make_unique<std::unordered_map<hgps::core::Identifier, double>>();
    trend_data.expected_income_boxcox =
        std::make_unique<std::unordered_map<hgps::core::Identifier, double>>();
    trend_data.income_steps =
        std::make_unique<std::unordered_map<hgps::core::Identifier, int>>();
    trend_data.income_models = std::make_unique<std::vector<hgps::LinearModelParams>>();
    trend_data.income_ranges =
        std::make_unique<std::vector<hgps::core::DoubleInterval>>();
    trend_data.income_lambda = std::make_unique<std::vector<double>>();
    trend_data.income_decay_factors =
        std::make_unique<std::unordered_map<hgps::core::Identifier, double>>();

    return trend_data;
}

bool append_staticlinear_trend_entry(StaticLinearTrendData &trend_data,
                                     hgps::TrendType trend_type,
                                     const hgps::core::Identifier &risk_factor_name,
                                     bool is_matrix_based_structure,
                                     const nlohmann::json *json_params,
                                     hgps::core::Diagnostics &diagnostics,
                                     std::string_view source_path,
                                     std::string_view field_path) {
    if (json_params == nullptr && !is_matrix_based_structure) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = std::string{field_path}},
                          "Missing legacy risk factor parameters");
        return false;
    }

    if (trend_type == hgps::TrendType::UPFTrend) {
        if (is_matrix_based_structure) {
            diagnostics.error(
                hgps::core::DiagnosticCode::invalid_value,
                {.source_path = std::string{source_path},
                 .field_path = std::string{field_path}},
                fmt::format("Matrix-based structure requires CSV-backed trend support for '{}'",
                            risk_factor_name.to_string()));
            return false;
        }

        if (!json_params->contains("Trend") || !(*json_params)["Trend"].is_object()) {
            diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                              {.source_path = std::string{source_path},
                               .field_path = join_field_path(field_path, "Trend")},
                              fmt::format("Trend is enabled but Trend data is missing for '{}'",
                                          risk_factor_name.to_string()));
            return false;
        }

        const auto &trend_json = (*json_params)["Trend"];
        const auto trend_field_path = join_field_path(field_path, "Trend");

        bool ok = true;
        auto trend_model =
            load_trend_model_from_json(trend_json, diagnostics, ok, source_path, trend_field_path);

        hgps::core::DoubleInterval trend_range;
        double trend_lambda = 0.0;

        get_to(trend_json, "Range", trend_range, diagnostics, ok, source_path, trend_field_path);
        get_to(trend_json, "Lambda", trend_lambda, diagnostics, ok, source_path, trend_field_path);

        if (!ok) {
            return false;
        }

        trend_data.models->emplace_back(std::move(trend_model));
        trend_data.ranges->emplace_back(trend_range);
        trend_data.lambda->emplace_back(trend_lambda);

        double expected_trend = 1.0;
        double expected_trend_boxcox = 1.0;
        int trend_steps = 0;

        if (json_params->contains("ExpectedTrend")) {
            expected_trend = (*json_params)["ExpectedTrend"].get<double>();
        }
        if (json_params->contains("ExpectedTrendBoxCox")) {
            expected_trend_boxcox = (*json_params)["ExpectedTrendBoxCox"].get<double>();
        }
        if (json_params->contains("TrendSteps")) {
            trend_steps = (*json_params)["TrendSteps"].get<int>();
        }

        (*trend_data.expected)[risk_factor_name] = expected_trend;
        (*trend_data.expected_boxcox)[risk_factor_name] = expected_trend_boxcox;
        (*trend_data.steps)[risk_factor_name] = trend_steps;
    } else {
        trend_data.models->emplace_back(hgps::LinearModelParams{});
        trend_data.ranges->emplace_back(hgps::core::DoubleInterval{0.0, 1.0});
        trend_data.lambda->emplace_back(1.0);
        (*trend_data.expected)[risk_factor_name] = 1.0;
        (*trend_data.expected_boxcox)[risk_factor_name] = 1.0;
        (*trend_data.steps)[risk_factor_name] = 0;
    }

    if (trend_type == hgps::TrendType::IncomeTrend) {
        if (is_matrix_based_structure) {
            diagnostics.error(
                hgps::core::DiagnosticCode::invalid_value,
                {.source_path = std::string{source_path},
                 .field_path = std::string{field_path}},
                fmt::format(
                    "Matrix-based structure requires CSV-backed income trend support for '{}'",
                    risk_factor_name.to_string()));
            return false;
        }

        if (!json_params->contains("IncomeTrend") || !(*json_params)["IncomeTrend"].is_object()) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = std::string{source_path},
                 .field_path = join_field_path(field_path, "IncomeTrend")},
                fmt::format("Income trend is enabled but IncomeTrend data is missing for '{}'",
                            risk_factor_name.to_string()));
            return false;
        }

        const auto &income_trend_json = (*json_params)["IncomeTrend"];
        const auto income_trend_field_path = join_field_path(field_path, "IncomeTrend");

        bool ok = true;
        auto income_trend_model = load_trend_model_from_json(income_trend_json, diagnostics, ok,
                                                             source_path, income_trend_field_path);

        hgps::core::DoubleInterval income_trend_range;
        double income_trend_lambda = 0.0;

        get_to(income_trend_json, "Range", income_trend_range, diagnostics, ok, source_path,
               income_trend_field_path);
        get_to(income_trend_json, "Lambda", income_trend_lambda, diagnostics, ok, source_path,
               income_trend_field_path);

        if (!ok) {
            return false;
        }

        if (!json_params->contains("ExpectedIncomeTrend")) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = std::string{source_path},
                 .field_path = join_field_path(field_path, "ExpectedIncomeTrend")},
                fmt::format("ExpectedIncomeTrend is missing for '{}'",
                            risk_factor_name.to_string()));
            return false;
        }

        if (!json_params->contains("ExpectedIncomeTrendBoxCox")) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = std::string{source_path},
                 .field_path = join_field_path(field_path, "ExpectedIncomeTrendBoxCox")},
                fmt::format("ExpectedIncomeTrendBoxCox is missing for '{}'",
                            risk_factor_name.to_string()));
            return false;
        }

        if (!json_params->contains("IncomeTrendSteps")) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = std::string{source_path},
                 .field_path = join_field_path(field_path, "IncomeTrendSteps")},
                fmt::format("IncomeTrendSteps is missing for '{}'",
                            risk_factor_name.to_string()));
            return false;
        }

        if (!json_params->contains("IncomeDecayFactor")) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = std::string{source_path},
                 .field_path = join_field_path(field_path, "IncomeDecayFactor")},
                fmt::format("IncomeDecayFactor is missing for '{}'",
                            risk_factor_name.to_string()));
            return false;
        }

        trend_data.income_models->emplace_back(std::move(income_trend_model));
        trend_data.income_ranges->emplace_back(income_trend_range);
        trend_data.income_lambda->emplace_back(income_trend_lambda);

        (*trend_data.expected_income)[risk_factor_name] =
            (*json_params)["ExpectedIncomeTrend"].get<double>();
        (*trend_data.expected_income_boxcox)[risk_factor_name] =
            (*json_params)["ExpectedIncomeTrendBoxCox"].get<double>();
        (*trend_data.income_steps)[risk_factor_name] =
            (*json_params)["IncomeTrendSteps"].get<int>();
        (*trend_data.income_decay_factors)[risk_factor_name] =
            (*json_params)["IncomeDecayFactor"].get<double>();
    }

    return true;
}

} // namespace hgps::input