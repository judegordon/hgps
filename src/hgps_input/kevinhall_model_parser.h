#pragma once

#include "config.h"
#include "hgps_core/diagnostics.h"

#include "hgps/kevin_hall_model.h"

#include <memory>
#include <string_view>

namespace hgps::input {

std::unique_ptr<hgps::KevinHallModelDefinition>
load_kevinhall_risk_model_definition(const nlohmann::json &opt,
                                     const Configuration &config,
                                     hgps::core::Diagnostics &diagnostics,
                                     std::string_view source_path = {});

} // namespace hgps::input