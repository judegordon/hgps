#pragma once

#include "config/config.h"
#include "hgps_core/diagnostics/input_issue.h"

#include "hgps/dynamic_hierarchical_linear_model.h"

#include <memory>
#include <string_view>

namespace hgps::input {

std::unique_ptr<hgps::DynamicHierarchicalLinearModelDefinition>
load_ebhlm_risk_model_definition(const nlohmann::json &opt,
                                 const Configuration &config,
                                 hgps::core::InputIssueReport &diagnostics,
                                 std::string_view source_path = {});

} // namespace hgps::input