#pragma once

#include "hgps_core/data/column.h"
#include "hgps_core/data/column_builder.h"
#include "hgps_core/data/data_table.h"
#include "hgps_core/types/identifier.h"

#include "config/config_types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace hgps::input {

hgps::core::DataTable load_datatable_from_csv(const FileInfo& file_info);

std::unordered_map<hgps::core::Identifier, std::vector<double>>
load_baseline_from_csv(const std::string& filename, const std::string& delimiter = ",");

} // namespace hgps::input