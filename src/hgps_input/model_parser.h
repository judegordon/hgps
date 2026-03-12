#pragma once

#include "HealthGPS/dummy_model.h"
#include "HealthGPS/dynamic_hierarchical_linear_model.h"
#include "HealthGPS/kevin_hall_model.h"
#include "HealthGPS/risk_factor_adjustable_model.h"
#include "HealthGPS/static_hierarchical_linear_model.h"
#include "HealthGPS/static_linear_model.h"

#include "configuration.h"
#include "jsonparser.h"

#include <filesystem>
#include <utility>

namespace hgps::input {

std::unique_ptr<hgps::RiskFactorSexAgeTable> load_risk_factor_expected(const Configuration &config);

std::unique_ptr<hgps::DummyModelDefinition>
load_dummy_risk_model_definition(hgps::RiskFactorModelType type, const nlohmann::json &opt);

std::unique_ptr<hgps::StaticHierarchicalLinearModelDefinition>
load_hlm_risk_model_definition(const nlohmann::json &opt);

std::unique_ptr<hgps::StaticLinearModelDefinition>
load_staticlinear_risk_model_definition(const nlohmann::json &opt, const Configuration &config);

std::unique_ptr<hgps::DynamicHierarchicalLinearModelDefinition>
load_ebhlm_risk_model_definition(const nlohmann::json &opt, const Configuration &config);

std::unique_ptr<hgps::KevinHallModelDefinition>
load_kevinhall_risk_model_definition(const nlohmann::json &opt, const Configuration &config);

std::unique_ptr<hgps::RiskFactorModelDefinition>
load_risk_model_definition(hgps::RiskFactorModelType model_type,
                           const std::filesystem::path &model_path, const Configuration &config);

nlohmann::json load_json(const std::filesystem::path &filepath);

void register_risk_factor_model_definitions(hgps::CachedRepository &repository,
                                            const Configuration &config);

} // namespace hgps::input
