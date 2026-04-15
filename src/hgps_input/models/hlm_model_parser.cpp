#include "hlm_model_parser.h"

#include "io/json_parser.h"

#include <fmt/core.h>

#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace hgps::input {

std::unique_ptr<hgps::StaticHierarchicalLinearModelDefinition>
load_hlm_risk_model_definition(const nlohmann::json &opt,
                               hgps::core::InputIssueReport &diagnostics,
                               std::string_view source_path) {
    HierarchicalModelInfo model_info;

    try {
        model_info.models = opt.at("models").get<std::unordered_map<std::string, LinearModelInfo>>();
    } catch (const nlohmann::json::out_of_range &) {
        diagnostics.error(hgps::core::IssueCode::missing_key,
                          {.source_path = std::string{source_path}, .field_path = "models"},
                          "Missing required key");
        return nullptr;
    } catch (const nlohmann::json::type_error &) {
        diagnostics.error(hgps::core::IssueCode::wrong_type,
                          {.source_path = std::string{source_path}, .field_path = "models"},
                          "Key has wrong type");
        return nullptr;
    } catch (const nlohmann::json::exception &e) {
        diagnostics.error(hgps::core::IssueCode::parse_failure,
                          {.source_path = std::string{source_path}, .field_path = "models"},
                          e.what());
        return nullptr;
    }

    try {
        model_info.levels =
            opt.at("levels").get<std::unordered_map<std::string, HierarchicalLevelInfo>>();
    } catch (const nlohmann::json::out_of_range &) {
        diagnostics.error(hgps::core::IssueCode::missing_key,
                          {.source_path = std::string{source_path}, .field_path = "levels"},
                          "Missing required key");
        return nullptr;
    } catch (const nlohmann::json::type_error &) {
        diagnostics.error(hgps::core::IssueCode::wrong_type,
                          {.source_path = std::string{source_path}, .field_path = "levels"},
                          "Key has wrong type");
        return nullptr;
    } catch (const nlohmann::json::exception &e) {
        diagnostics.error(hgps::core::IssueCode::parse_failure,
                          {.source_path = std::string{source_path}, .field_path = "levels"},
                          e.what());
        return nullptr;
    }

    std::map<int, hgps::HierarchicalLevel> levels;
    std::unordered_map<hgps::core::Identifier, hgps::LinearModel> models;

    for (const auto &[model_name, model_definition] : model_info.models) {
        std::unordered_map<hgps::core::Identifier, hgps::Coefficient> coefficients;

        for (const auto &[coefficient_name, coefficient_info] : model_definition.coefficients) {
            coefficients.emplace(hgps::core::Identifier{coefficient_name},
                                 hgps::Coefficient{.value = coefficient_info.value,
                                                   .pvalue = coefficient_info.pvalue,
                                                   .tvalue = coefficient_info.tvalue,
                                                   .std_error = coefficient_info.std_error});
        }

        models.emplace(hgps::core::Identifier{model_name},
                       hgps::LinearModel{
                           .coefficients = std::move(coefficients),
                           .residuals_standard_deviation =
                               model_definition.residuals_standard_deviation,
                           .rsquared = model_definition.rsquared,
                       });
    }

    for (auto &[level_key, level_info] : model_info.levels) {
        int level_index = 0;
        try {
            level_index = std::stoi(level_key);
        } catch (const std::exception &) {
            diagnostics.error(hgps::core::IssueCode::invalid_value,
                              {.source_path = std::string{source_path},
                               .field_path = "levels." + level_key},
                              fmt::format("Hierarchical level key '{}' is not a valid integer",
                                          level_key));
            return nullptr;
        }

        std::unordered_map<hgps::core::Identifier, int> variable_columns;
        const auto variable_count = static_cast<int>(level_info.variables.size());
        for (int index = 0; index < variable_count; ++index) {
            variable_columns.emplace(hgps::core::Identifier{level_info.variables[index]}, index);
        }

        levels.emplace(level_index,
                       hgps::HierarchicalLevel{
                           .variables = std::move(variable_columns),
                           .transition = hgps::core::DoubleArray2D(level_info.transition.rows,
                                                                   level_info.transition.cols,
                                                                   level_info.transition.data),
                           .inverse_transition =
                               hgps::core::DoubleArray2D(level_info.inverse_transition.rows,
                                                         level_info.inverse_transition.cols,
                                                         level_info.inverse_transition.data),
                           .residual_distribution =
                               hgps::core::DoubleArray2D(level_info.residual_distribution.rows,
                                                         level_info.residual_distribution.cols,
                                                         level_info.residual_distribution.data),
                           .correlation =
                               hgps::core::DoubleArray2D(level_info.correlation.rows,
                                                         level_info.correlation.cols,
                                                         level_info.correlation.data),
                           .variances = level_info.variances,
                       });
    }

    try {
        return std::make_unique<hgps::StaticHierarchicalLinearModelDefinition>(std::move(models),
                                                                               std::move(levels));
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::IssueCode::parse_failure,
                          {.source_path = std::string{source_path}},
                          e.what());
        return nullptr;
    } catch (...) {
        diagnostics.error(hgps::core::IssueCode::parse_failure,
                          {.source_path = std::string{source_path}},
                          "Unknown error while constructing HLM model definition");
        return nullptr;
    }
}

} // namespace hgps::input