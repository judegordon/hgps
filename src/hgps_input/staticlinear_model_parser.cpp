#include "staticlinear_model_parser.h"

#include "json_access.h"
#include "risk_factor_expected_loader.h"
#include "staticlinear_auxiliary_loader.h"
#include "staticlinear_csv_loader.h"
#include "staticlinear_trend_loader.h"

#include <Eigen/Cholesky>
#include <Eigen/Dense>
#include <fmt/core.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool has_new_errors(const hgps::core::Diagnostics &diagnostics,
                    const std::size_t initial_error_count) {
    return diagnostics.error_count() > initial_error_count;
}

bool is_reserved_matrix_row(std::string_view row_name) {
    return row_name == "Intercept" || row_name == "lambda" || row_name == "stddev" ||
           row_name == "min" || row_name == "max";
}

const nlohmann::json *find_legacy_risk_factor_params(const nlohmann::json &opt,
                                                     const hgps::core::Identifier &risk_factor_name,
                                                     hgps::core::Diagnostics &diagnostics,
                                                     std::string_view source_path) {
    if (!opt.contains("RiskFactorModels") || !opt["RiskFactorModels"].is_object()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "RiskFactorModels"},
                          "Missing required key");
        return nullptr;
    }

    for (const auto &[key, value] : opt["RiskFactorModels"].items()) {
        if (hgps::core::case_insensitive::equals(key, risk_factor_name.to_string())) {
            return &value;
        }
    }

    diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                      {.source_path = std::string{source_path},
                       .field_path = "RiskFactorModels." + risk_factor_name.to_string()},
                      fmt::format("Risk factor '{}' not found in RiskFactorModels",
                                  risk_factor_name.to_string()));
    return nullptr;
}

std::optional<double>
get_required_matrix_value(const hgps::input::MatrixCoefficientTable &table,
                          std::string_view row_name,
                          const std::string &risk_factor_name,
                          hgps::core::Diagnostics &diagnostics,
                          std::string_view source_path,
                          std::string_view field_path) {
    const auto row_it = table.rows.find(std::string{row_name});
    if (row_it == table.rows.end()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = hgps::input::join_field_path(field_path, row_name)},
                          fmt::format("Missing required coefficient row '{}'", row_name));
        return std::nullopt;
    }

    const auto value_it = row_it->second.find(risk_factor_name);
    if (value_it == row_it->second.end()) {
        diagnostics.error(
            hgps::core::DiagnosticCode::missing_key,
            {.source_path = std::string{source_path},
             .field_path = hgps::input::join_field_path(
                 hgps::input::join_field_path(field_path, row_name), risk_factor_name)},
            fmt::format("Missing required value for risk factor '{}'", risk_factor_name));
        return std::nullopt;
    }

    return value_it->second;
}

std::optional<hgps::LinearModelParams>
build_matrix_linear_model(const hgps::input::MatrixCoefficientTable &table,
                          const std::string &risk_factor_name,
                          hgps::core::Diagnostics &diagnostics,
                          std::string_view source_path,
                          std::string_view field_path,
                          bool include_log_coefficients = false) {
    hgps::LinearModelParams model;

    const auto intercept =
        get_required_matrix_value(table, "Intercept", risk_factor_name, diagnostics, source_path,
                                  field_path);
    if (!intercept.has_value()) {
        return std::nullopt;
    }
    model.intercept = *intercept;

    for (const auto &[row_name, coefficient_map] : table.rows) {
        if (is_reserved_matrix_row(row_name)) {
            continue;
        }

        const auto coeff_it = coefficient_map.find(risk_factor_name);
        if (coeff_it == coefficient_map.end()) {
            continue;
        }

        if (include_log_coefficients && row_name.starts_with("log_")) {
            model.log_coefficients[hgps::core::Identifier{row_name}] = coeff_it->second;
        } else {
            model.coefficients[hgps::core::Identifier{row_name}] = coeff_it->second;
        }
    }

    return model;
}

bool validate_expected_values_cover_risk_factors(
    const hgps::RiskFactorSexAgeTable &expected,
    const std::vector<hgps::core::Identifier> &risk_factor_names,
    hgps::core::Diagnostics &diagnostics,
    std::string_view source_path) {
    for (const auto &name : risk_factor_names) {
        if (!expected.at(hgps::core::Gender::male).contains(name)) {
            diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                              {.source_path = std::string{source_path},
                               .field_path = "baseline_adjustment.male"},
                              fmt::format(
                                  "'{}' is not defined in male risk factor expected values.",
                                  name.to_string()));
            return false;
        }

        if (!expected.at(hgps::core::Gender::female).contains(name)) {
            diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                              {.source_path = std::string{source_path},
                               .field_path = "baseline_adjustment.female"},
                              fmt::format(
                                  "'{}' is not defined in female risk factor expected values.",
                                  name.to_string()));
            return false;
        }
    }

    return true;
}

} // namespace

namespace hgps::input {

std::unique_ptr<hgps::StaticLinearModelDefinition>
load_staticlinear_risk_model_definition(const nlohmann::json &opt,
                                        const Configuration &config,
                                        hgps::core::Diagnostics &diagnostics,
                                        std::string_view source_path) {
    const auto initial_error_count = diagnostics.error_count();

    const auto trend_type =
        resolve_staticlinear_trend_type(config, diagnostics, source_path, "project_requirements");
    if (!trend_type.has_value()) {
        return nullptr;
    }

    auto trend_data = initialise_staticlinear_trend_data(*trend_type);

    const bool is_matrix_based_structure = is_matrix_based_risk_factor_structure(opt);

    const auto matrix_data =
        load_staticlinear_matrix_data(opt, config, diagnostics, source_path);
    if (!matrix_data.has_value()) {
        return nullptr;
    }

    std::optional<MatrixCoefficientTable> boxcox_coefficients;
    std::optional<MatrixCoefficientTable> policy_coefficients;
    std::optional<MatrixCoefficientTable> logistic_coefficients;

    if (is_matrix_based_structure) {
        if (!opt.contains("RiskFactorModels") || !opt["RiskFactorModels"].is_object()) {
            diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                              {.source_path = std::string{source_path},
                               .field_path = "RiskFactorModels"},
                              "Missing required key");
            return nullptr;
        }

        if (!opt["RiskFactorModels"].contains("boxcox_coefficients")) {
            diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                              {.source_path = std::string{source_path},
                               .field_path = "RiskFactorModels.boxcox_coefficients"},
                              "Missing required key");
            return nullptr;
        }

        if (!opt["RiskFactorModels"].contains("policy_coefficients")) {
            diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                              {.source_path = std::string{source_path},
                               .field_path = "RiskFactorModels.policy_coefficients"},
                              "Missing required key");
            return nullptr;
        }

        boxcox_coefficients = load_matrix_coefficient_table(
            opt["RiskFactorModels"]["boxcox_coefficients"], config, diagnostics, source_path,
            "RiskFactorModels.boxcox_coefficients");
        if (!boxcox_coefficients.has_value()) {
            return nullptr;
        }

        policy_coefficients = load_matrix_coefficient_table(
            opt["RiskFactorModels"]["policy_coefficients"], config, diagnostics, source_path,
            "RiskFactorModels.policy_coefficients");
        if (!policy_coefficients.has_value()) {
            return nullptr;
        }

        if (opt["RiskFactorModels"].contains("logistic_regression")) {
            logistic_coefficients = load_matrix_coefficient_table(
                opt["RiskFactorModels"]["logistic_regression"], config, diagnostics, source_path,
                "RiskFactorModels.logistic_regression");
            if (!logistic_coefficients.has_value()) {
                return nullptr;
            }
        }
    }

    std::vector<hgps::core::Identifier> names;
    std::vector<hgps::LinearModelParams> models;
    std::vector<hgps::core::DoubleInterval> ranges;
    std::vector<double> lambda;
    std::vector<double> stddev;
    std::vector<hgps::LinearModelParams> policy_models;
    std::vector<hgps::core::DoubleInterval> policy_ranges;
    std::vector<hgps::LinearModelParams> logistic_models;

    names.reserve(matrix_data->ordered_names.size());
    models.reserve(matrix_data->ordered_names.size());
    ranges.reserve(matrix_data->ordered_names.size());
    lambda.reserve(matrix_data->ordered_names.size());
    stddev.reserve(matrix_data->ordered_names.size());
    policy_models.reserve(matrix_data->ordered_names.size());
    policy_ranges.reserve(matrix_data->ordered_names.size());
    logistic_models.reserve(matrix_data->ordered_names.size());

    for (std::size_t index = 0; index < matrix_data->ordered_names.size(); ++index) {
        const auto &risk_factor_name = matrix_data->ordered_names[index];
        const auto risk_factor_name_str = risk_factor_name.to_string();
        const auto legacy_field_path =
            join_field_path("RiskFactorModels", risk_factor_name.to_string());

        const nlohmann::json *json_params = nullptr;
        if (!is_matrix_based_structure) {
            json_params =
                find_legacy_risk_factor_params(opt, risk_factor_name, diagnostics, source_path);
            if (json_params == nullptr) {
                return nullptr;
            }
        }

        names.emplace_back(risk_factor_name);

        hgps::LinearModelParams model;
        if (is_matrix_based_structure) {
            const auto built_model =
                build_matrix_linear_model(*boxcox_coefficients, risk_factor_name_str, diagnostics,
                                          source_path, "RiskFactorModels.boxcox_coefficients");
            if (!built_model.has_value()) {
                return nullptr;
            }
            model = *built_model;
        } else {
            bool ok = true;
            get_to(*json_params, "Intercept", model.intercept, diagnostics, ok, source_path,
                   legacy_field_path);
            get_to(*json_params, "Coefficients", model.coefficients, diagnostics, ok, source_path,
                   legacy_field_path);
            if (!ok) {
                return nullptr;
            }
        }
        models.emplace_back(std::move(model));

        if (is_matrix_based_structure) {
            const auto min_value =
                get_required_matrix_value(*boxcox_coefficients, "min", risk_factor_name_str,
                                          diagnostics, source_path,
                                          "RiskFactorModels.boxcox_coefficients");
            const auto max_value =
                get_required_matrix_value(*boxcox_coefficients, "max", risk_factor_name_str,
                                          diagnostics, source_path,
                                          "RiskFactorModels.boxcox_coefficients");
            const auto lambda_value =
                get_required_matrix_value(*boxcox_coefficients, "lambda", risk_factor_name_str,
                                          diagnostics, source_path,
                                          "RiskFactorModels.boxcox_coefficients");
            const auto stddev_value =
                get_required_matrix_value(*boxcox_coefficients, "stddev", risk_factor_name_str,
                                          diagnostics, source_path,
                                          "RiskFactorModels.boxcox_coefficients");

            if (!min_value.has_value() || !max_value.has_value() || !lambda_value.has_value() ||
                !stddev_value.has_value()) {
                return nullptr;
            }

            ranges.emplace_back(*min_value, *max_value);
            lambda.emplace_back(*lambda_value);
            stddev.emplace_back(*stddev_value);
        } else {
            bool ok = true;
            hgps::core::DoubleInterval range;
            double lambda_value = 0.0;
            double stddev_value = 0.0;

            get_to(*json_params, "Range", range, diagnostics, ok, source_path, legacy_field_path);
            get_to(*json_params, "Lambda", lambda_value, diagnostics, ok, source_path,
                   legacy_field_path);
            get_to(*json_params, "StdDev", stddev_value, diagnostics, ok, source_path,
                   legacy_field_path);

            if (!ok) {
                return nullptr;
            }

            ranges.emplace_back(range);
            lambda.emplace_back(lambda_value);
            stddev.emplace_back(stddev_value);
        }

        hgps::LinearModelParams policy_model;
        if (is_matrix_based_structure) {
            const auto built_policy_model =
                build_matrix_linear_model(*policy_coefficients, risk_factor_name_str, diagnostics,
                                          source_path, "RiskFactorModels.policy_coefficients", true);
            if (!built_policy_model.has_value()) {
                return nullptr;
            }
            policy_model = *built_policy_model;
        } else {
            if (!json_params->contains("Policy") || !(*json_params)["Policy"].is_object()) {
                diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                                  {.source_path = std::string{source_path},
                                   .field_path = join_field_path(legacy_field_path, "Policy")},
                                  "Missing required key");
                return nullptr;
            }

            const auto &policy_json = (*json_params)["Policy"];
            const auto policy_field_path = join_field_path(legacy_field_path, "Policy");

            bool ok = true;
            get_to(policy_json, "Intercept", policy_model.intercept, diagnostics, ok, source_path,
                   policy_field_path);
            get_to(policy_json, "Coefficients", policy_model.coefficients, diagnostics, ok,
                   source_path, policy_field_path);
            get_to(policy_json, "LogCoefficients", policy_model.log_coefficients, diagnostics, ok,
                   source_path, policy_field_path);

            if (!ok) {
                return nullptr;
            }
        }
        policy_models.emplace_back(std::move(policy_model));

        if (!hgps::core::case_insensitive::equals(
                risk_factor_name.to_string(),
                matrix_data->policy_ordered_names[matrix_data->policy_column_mapping[index]]
                    .to_string())) {
            diagnostics.error(
                hgps::core::DiagnosticCode::invalid_value,
                {.source_path = std::string{source_path},
                 .field_path = "PolicyCovarianceFile"},
                fmt::format("Risk factor '{}' does not match policy covariance ordering",
                            risk_factor_name.to_string()));
            return nullptr;
        }

        if (is_matrix_based_structure) {
            const auto policy_min =
                get_required_matrix_value(*policy_coefficients, "min", risk_factor_name_str,
                                          diagnostics, source_path,
                                          "RiskFactorModels.policy_coefficients");
            const auto policy_max =
                get_required_matrix_value(*policy_coefficients, "max", risk_factor_name_str,
                                          diagnostics, source_path,
                                          "RiskFactorModels.policy_coefficients");

            if (!policy_min.has_value() || !policy_max.has_value()) {
                return nullptr;
            }

            policy_ranges.emplace_back(*policy_min, *policy_max);
        } else {
            const auto &policy_json = (*json_params)["Policy"];
            const auto policy_field_path = join_field_path(legacy_field_path, "Policy");

            bool ok = true;
            hgps::core::DoubleInterval policy_range;
            get_to(policy_json, "Range", policy_range, diagnostics, ok, source_path,
                   policy_field_path);
            if (!ok) {
                return nullptr;
            }

            policy_ranges.emplace_back(policy_range);
        }

        hgps::LinearModelParams logistic_model;
        if (is_matrix_based_structure && logistic_coefficients.has_value() &&
            logistic_coefficients->rows.contains("Intercept") &&
            logistic_coefficients->rows.at("Intercept").contains(risk_factor_name_str)) {
            const auto built_logistic_model =
                build_matrix_linear_model(*logistic_coefficients, risk_factor_name_str, diagnostics,
                                          source_path, "RiskFactorModels.logistic_regression");
            if (!built_logistic_model.has_value()) {
                return nullptr;
            }
            logistic_model = *built_logistic_model;
        }
        logistic_models.emplace_back(std::move(logistic_model));

        if (!append_staticlinear_trend_entry(
                trend_data, *trend_type, risk_factor_name, is_matrix_based_structure, json_params,
                diagnostics, source_path, legacy_field_path)) {
            return nullptr;
        }
    }

    if (!is_matrix_based_structure && opt.contains("RiskFactorModels") &&
        opt["RiskFactorModels"].is_object() &&
        opt["RiskFactorModels"].size() != matrix_data->ordered_names.size()) {
        diagnostics.error(
            hgps::core::DiagnosticCode::invalid_value,
            {.source_path = std::string{source_path}, .field_path = "RiskFactorModels"},
            fmt::format("Risk factor count ({}) does not match correlation matrix column count ({})",
                        opt["RiskFactorModels"].size(),
                        matrix_data->ordered_names.size()));
        return nullptr;
    }

    Eigen::LLT<Eigen::MatrixXd> correlation_llt{matrix_data->correlation};
    if (correlation_llt.info() != Eigen::Success) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = std::string{source_path},
                           .field_path = "RiskFactorCorrelationFile"},
                          "Risk factor correlation matrix is not positive definite");
        return nullptr;
    }
    auto cholesky = Eigen::MatrixXd{correlation_llt.matrixL()};

    Eigen::LLT<Eigen::MatrixXd> policy_llt{matrix_data->policy_covariance};
    if (policy_llt.info() != Eigen::Success) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = std::string{source_path},
                           .field_path = "PolicyCovarianceFile"},
                          "Policy covariance matrix is not positive definite");
        return nullptr;
    }
    auto policy_cholesky = Eigen::MatrixXd{policy_llt.matrixL()};

    auto expected = load_risk_factor_expected(config, diagnostics);
    if (!expected) {
        return nullptr;
    }

    if (!validate_expected_values_cover_risk_factors(*expected, names, diagnostics, source_path)) {
        return nullptr;
    }

    double info_speed = 0.0;
    if (!get_to(opt, "InformationSpeed", info_speed, diagnostics, source_path)) {
        return nullptr;
    }

    auto rural_prevalence = load_rural_prevalence(opt, diagnostics, source_path);
    if (!rural_prevalence.has_value()) {
        return nullptr;
    }

    const auto income_data = load_income_model_data(opt, config, diagnostics, source_path);
    if (!income_data.has_value()) {
        return nullptr;
    }

    const auto physical_activity_data =
        load_physical_activity_model_data(opt, config, diagnostics, source_path);
    if (!physical_activity_data.has_value()) {
        return nullptr;
    }

    const bool has_active_policies = detect_active_policies(policy_models);

    if (has_new_errors(diagnostics, initial_error_count)) {
        return nullptr;
    }

    try {
        return std::make_unique<hgps::StaticLinearModelDefinition>(
            std::move(expected), std::move(trend_data.expected), std::move(trend_data.steps),
            std::move(trend_data.expected_boxcox), std::move(names), std::move(models),
            std::move(ranges), std::move(lambda), std::move(stddev), std::move(cholesky),
            std::move(policy_models), std::move(policy_ranges), std::move(policy_cholesky),
            std::move(trend_data.models), std::move(trend_data.ranges),
            std::move(trend_data.lambda), info_speed, std::move(*rural_prevalence),
            std::move(income_data->models), physical_activity_data->stddev, *trend_type,
            std::move(trend_data.expected_income), std::move(trend_data.expected_income_boxcox),
            std::move(trend_data.income_steps), std::move(trend_data.income_models),
            std::move(trend_data.income_ranges), std::move(trend_data.income_lambda),
            std::move(trend_data.income_decay_factors), income_data->is_continuous_model,
            std::move(income_data->continuous_model), std::move(income_data->income_categories),
            std::move(physical_activity_data->models), has_active_policies,
            std::move(logistic_models));
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = std::string{source_path}},
                          e.what());
        return nullptr;
    } catch (...) {
        diagnostics.error(
            hgps::core::DiagnosticCode::parse_failure,
            {.source_path = std::string{source_path}},
            "Unknown error while constructing static linear model definition");
        return nullptr;
    }
}

} // namespace hgps::input