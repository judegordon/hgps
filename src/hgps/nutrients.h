#pragma once

#include "HealthGPS.Core/identifier.h"

#include "rapidcsv.h"

#include <unordered_map>
#include <vector>

namespace hgps {
namespace detail {
std::vector<size_t> get_nutrient_indexes(const std::vector<std::string> &column_names,
                                         const std::vector<core::Identifier> &nutrients);

std::unordered_map<core::Identifier, std::vector<double>>
get_nutrient_table(rapidcsv::Document &doc, const std::vector<core::Identifier> &nutrients,
                   const std::vector<size_t> &nutrient_idx);
} // namespace detail

std::unordered_map<core::Identifier, std::vector<double>>
load_nutrient_table(const std::string &csv_path, const std::vector<core::Identifier> &nutrients);
} // namespace hgps
