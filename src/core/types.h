// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/forward_type.h.
#pragma once

#include <cstdint>
#include <type_traits>

namespace hgps::core {

/// @brief How much the program says about what it is doing.
enum class VerboseMode : std::uint8_t { none, verbose };

enum class Gender : std::uint8_t { unknown, male, female };

enum class DiseaseGroup : std::uint8_t { other, cancer };

enum class Sector : std::uint8_t { unknown, urban, rural };

/// @brief Income categories.
///
/// The enumerators are ordered from lowest to highest deliberately: this order is the sampling
/// order for income assignment, and it is part of the model definition rather than an artefact
/// of a container. See docs/decisions/0016-categorical-ordered-sampling.md.
enum class Income : std::uint8_t { unknown, low, lowermiddle, middle, uppermiddle, high };

/// @brief A row of a table looked up by value, holding one number per sex.
struct LookupGenderValue {
    int value{};
    double male{};
    double female{};
};

/// @brief Arithmetic types, for the numeric containers.
template <typename T>
concept Numerical = std::is_arithmetic_v<T>;

class DataTable;
class DataTableColumn;
class DataTableColumnVisitor;

} // namespace hgps::core
