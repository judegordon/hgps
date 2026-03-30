#pragma once

#include "config/config.h"
#include "hgps_core/diagnostics.h"

#include "hgps/risk_factor_adjustable_model.h"

#include <filesystem>
#include <memory>

namespace hgps {
class CachedRepository;
}

namespace hgps::input {

std::unique_ptr<hgps::RiskFactorModelDefinition>
load_risk_model_definition(hgps::RiskFactorModelType model_type,
                           const std::filesystem::path &model_path,
                           const Configuration &config,
                           hgps::core::Diagnostics &diagnostics);

void register_risk_factor_model_definitions(hgps::CachedRepository &repository,
                                            const Configuration &config,
                                            hgps::core::Diagnostics &diagnostics);

} // namespace hgps::input