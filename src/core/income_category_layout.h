// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/income_category_layout.h, income_category_layout.cpp.
#pragma once

#include "types.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hgps::core {

/// @brief The ordered income buckets a project uses, from project_requirements.income.categories.
///
/// The order here IS the sampling order for income assignment and the column order of the
/// income-stratified output. Nothing derives an income order from a container's iteration
/// (audit B-05); rng::Categorical takes `strata` as its ordered value sequence.
struct IncomeCategoryLayout {
    std::size_t count{};
    std::vector<Income> strata;
    std::vector<std::string> labels;

    std::span<const Income> ordered_strata() const noexcept { return strata; }
};

/// @brief Builds the layout for "3", "4" or "5".
/// @throws std::invalid_argument for any other value.
IncomeCategoryLayout income_category_layout_from_config(std::string_view categories);

/// @brief The 0-based table index of an income category in this layout.
/// @throws std::invalid_argument if the category is not part of the layout.
std::size_t income_table_index(Income income, const IncomeCategoryLayout &layout);

/// @brief The income category for an equal-split rank bucket, clamped to the top bucket.
Income income_from_equal_split_bucket(std::size_t bucket, const IncomeCategoryLayout &layout);

/// @brief The numeric encoding used by the analysis outputs.
/// @throws std::invalid_argument if the category is not part of the layout.
double income_category_numeric(Income income, const IncomeCategoryLayout &layout);

/// @brief The lower-case name of an income category, for diagnostics and output file names.
std::string_view income_name(Income income) noexcept;

} // namespace hgps::core
