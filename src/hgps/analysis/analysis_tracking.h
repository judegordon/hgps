#pragma once

#include "analysis_definition.h"
#include "hgps_input/config/config_types.h"
#include "simulation/runtime_context.h"

#include <optional>

namespace hgps::analysis {

void publish_individual_tracking_if_enabled(
    RuntimeContext &context,
    const std::optional<hgps::input::IndividualIdTrackingConfig> &config);

} // namespace hgps::analysis