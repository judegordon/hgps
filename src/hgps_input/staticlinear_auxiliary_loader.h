#pragma once

#include "config.h"
#include "hgps_core/diagnostics.h"

#include "hgps/static_linear_model.h"

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace hgps::input {

struct StaticLinearIncomeModelData {
    std::unordered_map<hgps::core::Income, hgps::LinearModelParams> models;
    bool is_continuous_model{false};
    hgps::LinearModelParams continuous_model;
    std::string income_categories;
};

struct StaticLinearPhysicalActivityModelData {
    std::unordered_map<hgps::core::Identifier, hgps::PhysicalActivityModel> models;
    double stddev{0.0};
};

std::optional<std::unordered_map<hgps::core::Identifier,
                                 std::unordered_map<hgps::core::Gender, double>>>
load_rural_prevalence(const nlohmann::json &opt,
                      hgps::core::Diagnostics &diagnostics,
                      std::string_view source_path = {});

std::optional<StaticLinearIncomeModelData>
load_income_model_data(const nlohmann::json &opt,
                       const Configuration &config,
                       hgps::core::Diagnostics &diagnostics,
                       std::string_view source_path = {});

std::optional<StaticLinearPhysicalActivityModelData>
load_physical_activity_model_data(const nlohmann::json &opt,
                                  const Configuration &config,
                                  hgps::core::Diagnostics &diagnostics,
                                  std::string_view source_path = {});

bool detect_active_policies(const std::vector<hgps::LinearModelParams> &policy_models);

} // namespace hgps::input