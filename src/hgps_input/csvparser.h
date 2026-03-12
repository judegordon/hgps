#pragma once

#include "HealthGPS.Core/api.h"
#include "poco.h"

namespace hgps::input {

hgps::core::DataTable load_datatable_from_csv(const FileInfo &file_info);

std::unordered_map<hgps::core::Identifier, std::vector<double>>
load_baseline_from_csv(const std::string &filename, const std::string &delimiter = ",");

} // namespace hgps::input
