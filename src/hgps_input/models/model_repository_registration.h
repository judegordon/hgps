#pragma once

#include "config/config.h"
#include "hgps_core/diagnostics.h"

namespace hgps {
class CachedRepository;
}

namespace hgps::input {

void register_risk_factor_model_definitions(hgps::CachedRepository &repository,
                                            const Configuration &config,
                                            hgps::core::InputIssueReport &diagnostics);

} // namespace hgps::input