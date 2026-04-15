#pragma once

#include "config/config.h"
#include "hgps_core/diagnostics.h"

#include "hgps/static_linear_model.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hgps::input {

struct StaticLinearTrendData {
    std::unique_ptr<std::vector<hgps::LinearModelParams>> models;
    std::unique_ptr<std::vector<hgps::core::DoubleInterval>> ranges;
    std::unique_ptr<std::vector<double>> lambda;
    std::unique_ptr<std::unordered_map<hgps::core::Identifier, double>> expected;
    std::unique_ptr<std::unordered_map<hgps::core::Identifier, double>> expected_boxcox;
    std::unique_ptr<std::unordered_map<hgps::core::Identifier, int>> steps;

    std::unique_ptr<std::unordered_map<hgps::core::Identifier, double>> expected_income;
    std::unique_ptr<std::unordered_map<hgps::core::Identifier, double>> expected_income_boxcox;
    std::unique_ptr<std::unordered_map<hgps::core::Identifier, int>> income_steps;
    std::unique_ptr<std::vector<hgps::LinearModelParams>> income_models;
    std::unique_ptr<std::vector<hgps::core::DoubleInterval>> income_ranges;
    std::unique_ptr<std::vector<double>> income_lambda;
    std::unique_ptr<std::unordered_map<hgps::core::Identifier, double>> income_decay_factors;
};

std::optional<hgps::TrendType>
resolve_staticlinear_trend_type(const Configuration &config,
                                hgps::core::InputIssueReport &diagnostics,
                                std::string_view source_path = {},
                                std::string_view field_path = {});

StaticLinearTrendData initialise_staticlinear_trend_data(hgps::TrendType trend_type);

bool append_staticlinear_trend_entry(StaticLinearTrendData &trend_data,
                                     hgps::TrendType trend_type,
                                     const hgps::core::Identifier &risk_factor_name,
                                     bool is_matrix_based_structure,
                                     const nlohmann::json *json_params,
                                     hgps::core::InputIssueReport &diagnostics,
                                     std::string_view source_path = {},
                                     std::string_view field_path = {});

} // namespace hgps::input