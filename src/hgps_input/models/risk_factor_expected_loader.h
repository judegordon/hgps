#pragma once

#include "config/config.h"
#include "hgps_core/diagnostics/input_issue.h"

#include "hgps/risk_factor_adjustable_model.h"

#include <memory>

namespace hgps::input {

std::unique_ptr<hgps::RiskFactorSexAgeTable>
load_risk_factor_expected(const Configuration &config,
                          hgps::core::InputIssueReport &diagnostics);

} // namespace hgps::input