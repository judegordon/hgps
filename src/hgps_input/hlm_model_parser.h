```cpp
#pragma once

#include "config.h"
#include "hgps_core/diagnostics.h"

#include "hgps/static_hierarchical_linear_model.h"

#include <memory>
#include <string_view>

namespace hgps::input {

std::unique_ptr<hgps::StaticHierarchicalLinearModelDefinition>
load_hlm_risk_model_definition(const nlohmann::json &opt,
                               hgps::core::Diagnostics &diagnostics,
                               std::string_view source_path = {});

} // namespace hgps::input
```
