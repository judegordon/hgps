#include "staticlinear_auxiliary_loader.h"

#include "config_section_parsing.h"
#include "json_access.h"
#include "json_parser.h"
#include "model_parser_common.h"

#include <fmt/core.h>
#include <rapidcsv.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

std::string normalise_delimiter(std::string delimiter) {
    if (delimiter == "\\t") {
        return "\t";
    }

    if (delimiter.empty()) {
        return ",";
    }

    return delimiter;
}

std::optional<rapidcsv::Document>
open_unlabelled_two_column_csv(const std::filesystem::path &path,
                               std::string_view delimiter,
                               hgps::core::Diagnostics &diagnostics,
                               std::string_view field_path) {
    try {
        const auto normalised_delimiter = normalise_delimiter(std::string{delimiter});
        return rapidcsv::Document(path.string(), rapidcsv::LabelParams(-1, -1),
                                  rapidcsv::SeparatorParams{normalised_delimiter.front()});
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = path.string(), .field_path = std::string{field_path}},
                          e.what());
        return std::nullopt;
    }
}

std::optional<hgps::LinearModelParams>
load_two_column_linear_model_csv(const nlohmann::json &node,
                                 const std::filesystem::path &root_path,
                                 hgps::core::Diagnostics &diagnostics,
                                 std::string_view source_path,
                                 std::string_view field_path,
                                 bool skip_first_row) {
    const auto file_info = hgps::input::get_file_info(node, root_path, diagnostics, source_path,
                                                      field_path);
    if (!file_info.has_value()) {
        return std::nullopt;
    }

    const auto doc =
        open_unlabelled_two_column_csv(file_info->name, file_info->delimiter, diagnostics,
                                       field_path);
    if (!doc.has_value()) {
        return std::nullopt;
    }

    if (doc->GetColumnCount() != 2) {
        diagnostics.error(
            hgps::core::DiagnosticCode::invalid_value,
            {.source_path = file_info->name.string(), .field_path = std::string{field_path}},
            fmt::format("CSV file must have exactly 2 columns. Found {}", doc->GetColumnCount()));
        return std::nullopt;
    }

    hgps::LinearModelParams model;

    const std::size_t row_start = skip_first_row ? 1U : 0U;
    for (std::size_t row = row_start; row < doc->GetRowCount(); ++row) {
        try {
            const auto factor_name = doc->GetCell<std::string>(0, row);
            const auto coefficient_value = doc->GetCell<double>(1, row);

            if (factor_name == "Intercept") {
                model.intercept = coefficient_value;
            } else {
                model.coefficients[hgps::core::Identifier{factor_name}] = coefficient_value;
            }
        } catch (const std::exception &e) {
            diagnostics.error(
                hgps::core::DiagnosticCode::parse_failure,
                {.source_path = file_info->name.string(), .field_path = std::string{field_path}},
                fmt::format("Failed reading row {}: {}", row, e.what()));
            return std::nullopt;
        }
    }

    return model;
}

std::optional<hgps::PhysicalActivityModel>
load_continuous_physical_activity_model(const nlohmann::json &node,
                                        const std::filesystem::path &root_path,
                                        hgps::core::Diagnostics &diagnostics,
                                        std::string_view source_path,
                                        std::string_view field_path) {
    const auto file_info = hgps::input::get_file_info(node, root_path, diagnostics, source_path,
                                                      field_path);
    if (!file_info.has_value()) {
        return std::nullopt;
    }

    const auto doc =
        open_unlabelled_two_column_csv(file_info->name, file_info->delimiter, diagnostics,
                                       field_path);
    if (!doc.has_value()) {
        return std::nullopt;
    }

    if (doc->GetColumnCount() != 2) {
        diagnostics.error(
            hgps::core::DiagnosticCode::invalid_value,
            {.source_path = file_info->name.string(), .field_path = std::string{field_path}},
            fmt::format("Physical activity CSV file must have exactly 2 columns. Found {}",
                        doc->GetColumnCount()));
        return std::nullopt;
    }

    hgps::PhysicalActivityModel model;
    model.model_type = "continuous";

    for (std::size_t row = 0; row < doc->GetRowCount(); ++row) {
        try {
            const auto factor_name = doc->GetCell<std::string>(0, row);
            const auto coefficient_value = doc->GetCell<double>(1, row);

            if (factor_name == "Intercept") {
                model.intercept = coefficient_value;
            } else if (factor_name == "min") {
                model.min_value = coefficient_value;
            } else if (factor_name == "max") {
                model.max_value = coefficient_value;
            } else if (factor_name == "stddev") {
                model.stddev = coefficient_value;
            } else {
                model.coefficients[hgps::core::Identifier{factor_name}] = coefficient_value;
            }
        } catch (const std::exception &e) {
            diagnostics.error(
                hgps::core::DiagnosticCode::parse_failure,
                {.source_path = file_info->name.string(), .field_path = std::string{field_path}},
                fmt::format("Failed reading physical activity row {}: {}", row, e.what()));
            return std::nullopt;
        }
    }

    return model;
}

} // namespace

namespace hgps::input {

std::optional<std::unordered_map<hgps::core::Identifier,
                                 std::unordered_map<hgps::core::Gender, double>>>
load_rural_prevalence(const nlohmann::json &opt,
                      hgps::core::Diagnostics &diagnostics,
                      std::string_view source_path) {
    if (!opt.contains("RuralPrevalence")) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "RuralPrevalence"},
                          "Missing required key");
        return std::nullopt;
    }

    if (!opt["RuralPrevalence"].is_array()) {
        diagnostics.error(hgps::core::DiagnosticCode::wrong_type,
                          {.source_path = std::string{source_path},
                           .field_path = "RuralPrevalence"},
                          "Key has wrong type");
        return std::nullopt;
    }

    std::unordered_map<hgps::core::Identifier, std::unordered_map<hgps::core::Gender, double>>
        rural_prevalence;

    for (std::size_t i = 0; i < opt["RuralPrevalence"].size(); ++i) {
        const auto &item = opt["RuralPrevalence"][i];
        const auto item_path = fmt::format("RuralPrevalence[{}]", i);

        bool ok = true;
        hgps::core::Identifier age_group_name;
        double female = 0.0;
        double male = 0.0;

        get_to(item, "Name", age_group_name, diagnostics, ok, source_path, item_path);
        get_to(item, "Female", female, diagnostics, ok, source_path, item_path);
        get_to(item, "Male", male, diagnostics, ok, source_path, item_path);

        if (!ok) {
            return std::nullopt;
        }

        rural_prevalence[age_group_name] = {
            {hgps::core::Gender::female, female},
            {hgps::core::Gender::male, male},
        };
    }

    return rural_prevalence;
}

std::optional<StaticLinearIncomeModelData>
load_income_model_data(const nlohmann::json &opt,
                       const Configuration &config,
                       hgps::core::Diagnostics &diagnostics,
                       std::string_view source_path) {
    if (!opt.contains("IncomeModels") || !opt["IncomeModels"].is_object()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "IncomeModels"},
                          "Missing required key");
        return std::nullopt;
    }

    StaticLinearIncomeModelData data;

    const auto &inc_req = config.project_requirements.income;
    data.income_categories = inc_req.categories;

    if (data.income_categories != 3 && data.income_categories != 4) {
        diagnostics.error(
            hgps::core::DiagnosticCode::invalid_value,
            {.source_path = std::string{source_path},
             .field_path = "project_requirements.income.categories"},
            fmt::format(R"(income.categories must be 3 or 4. Got "{}")",
                        data.income_categories));
        return std::nullopt;
    }

    data.is_continuous_model = (inc_req.type == "continuous");
    const bool model_has_continuous = opt["IncomeModels"].contains("continuous");

    if (data.is_continuous_model) {
        if (!model_has_continuous) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = std::string{source_path},
                 .field_path = "IncomeModels.continuous"},
                R"(income.type is "continuous" but IncomeModels has no "continuous" entry)");
            return std::nullopt;
        }

        const auto &continuous_json = opt["IncomeModels"]["continuous"];

        if (!continuous_json.contains("csv_file")) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = std::string{source_path},
                 .field_path = "IncomeModels.continuous.csv_file"},
                "Continuous income model must specify 'csv_file'");
            return std::nullopt;
        }

        const auto continuous_model =
            load_two_column_linear_model_csv(continuous_json, config.root_path, diagnostics,
                                             source_path, "IncomeModels.continuous", true);
        if (!continuous_model.has_value()) {
            return std::nullopt;
        }

        data.continuous_model = *continuous_model;

        data.models.emplace(hgps::core::Income::low, hgps::LinearModelParams{});
        if (data.income_categories == 4) {
            data.models.emplace(hgps::core::Income::lowermiddle, hgps::LinearModelParams{});
            data.models.emplace(hgps::core::Income::uppermiddle, hgps::LinearModelParams{});
        } else {
            data.models.emplace(hgps::core::Income::middle, hgps::LinearModelParams{});
        }
        data.models.emplace(hgps::core::Income::high, hgps::LinearModelParams{});
    } else {
        if (model_has_continuous && opt["IncomeModels"].size() == 1U) {
            diagnostics.error(
                hgps::core::DiagnosticCode::invalid_value,
                {.source_path = std::string{source_path}, .field_path = "IncomeModels"},
                R"(income.type is "categorical" but IncomeModels only has "continuous")");
            return std::nullopt;
        }

        for (const auto &[key, json_params] : opt["IncomeModels"].items()) {
            if (key == "simple" || key == "continuous") {
                continue;
            }

            const auto category = map_income_category(
                key, data.income_categories, diagnostics, source_path, "IncomeModels");
            if (diagnostics.has_errors()) {
                return std::nullopt;
            }

            hgps::LinearModelParams model;
            bool ok = true;
            get_to(json_params, "Intercept", model.intercept, diagnostics, ok, source_path,
                   fmt::format("IncomeModels.{}", key));
            get_to(json_params, "Coefficients", model.coefficients, diagnostics, ok, source_path,
                   fmt::format("IncomeModels.{}", key));
            if (!ok) {
                return std::nullopt;
            }

            data.models.emplace(category, std::move(model));
        }
    }

    return data;
}

std::optional<StaticLinearPhysicalActivityModelData>
load_physical_activity_model_data(const nlohmann::json &opt,
                                  const Configuration &config,
                                  hgps::core::Diagnostics &diagnostics,
                                  std::string_view source_path) {
    StaticLinearPhysicalActivityModelData data;

    const auto &pa_req = config.project_requirements.physical_activity;
    if (!pa_req.enabled) {
        return data;
    }

    if (!opt.contains("PhysicalActivityModels") || !opt["PhysicalActivityModels"].is_object()) {
        if (opt.contains("PhysicalActivityStdDev")) {
            double stddev = 0.0;
            if (!get_to(opt, "PhysicalActivityStdDev", stddev, diagnostics, source_path)) {
                return std::nullopt;
            }
            data.stddev = stddev;
            return data;
        }

        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "PhysicalActivityModels"},
                          "Missing required key");
        return std::nullopt;
    }

    const auto &pa_type = pa_req.type;
    if (!opt["PhysicalActivityModels"].contains(pa_type)) {
        diagnostics.error(
            hgps::core::DiagnosticCode::missing_key,
            {.source_path = std::string{source_path},
             .field_path = fmt::format("PhysicalActivityModels.{}", pa_type)},
            fmt::format("PhysicalActivityModels has no '{}' entry", pa_type));
        return std::nullopt;
    }

    const auto &model_config = opt["PhysicalActivityModels"][pa_type];

    if (pa_type == "simple") {
        hgps::PhysicalActivityModel model;
        model.model_type = "simple";

        if (model_config.contains("PhysicalActivityStdDev")) {
            if (!get_to(model_config, "PhysicalActivityStdDev", model.stddev, diagnostics,
                        source_path, "PhysicalActivityModels.simple")) {
                return std::nullopt;
            }
        } else if (opt.contains("PhysicalActivityStdDev")) {
            if (!get_to(opt, "PhysicalActivityStdDev", model.stddev, diagnostics, source_path)) {
                return std::nullopt;
            }
        } else {
            model.stddev = 0.0;
        }

        data.stddev = model.stddev;
        data.models[hgps::core::Identifier{"simple"}] = std::move(model);
        return data;
    }

    if (pa_type == "continuous") {
        if (!model_config.contains("csv_file")) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = std::string{source_path},
                 .field_path = "PhysicalActivityModels.continuous.csv_file"},
                "Continuous physical activity model must specify 'csv_file'");
            return std::nullopt;
        }

        const auto model =
            load_continuous_physical_activity_model(model_config, config.root_path, diagnostics,
                                                    source_path, "PhysicalActivityModels.continuous");
        if (!model.has_value()) {
            return std::nullopt;
        }

        data.stddev = model->stddev;
        data.models[hgps::core::Identifier{"continuous"}] = *model;
        return data;
    }

    diagnostics.error(hgps::core::DiagnosticCode::invalid_enum_value,
                      {.source_path = std::string{source_path},
                       .field_path = "project_requirements.physical_activity.type"},
                      fmt::format("Unsupported physical activity type '{}'", pa_type));
    return std::nullopt;
}

bool detect_active_policies(const std::vector<hgps::LinearModelParams> &policy_models) {
    for (const auto &policy_model : policy_models) {
        if (policy_model.intercept != 0.0) {
            return true;
        }

        for (const auto &[unused_name, value] : policy_model.coefficients) {
            if (value != 0.0) {
                return true;
            }
        }

        for (const auto &[unused_name, value] : policy_model.log_coefficients) {
            if (value != 0.0) {
                return true;
            }
        }
    }

    return false;
}

} // namespace hgps::input