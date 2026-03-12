#pragma once
#include <cstdint>
#include <type_traits>

// forward type declaration
namespace hgps::core {

enum class VerboseMode : uint8_t {
    none,

    verbose
};

enum class Gender : uint8_t {
    unknown,

    male,

    female
};

enum class DiseaseGroup : uint8_t {
    other,

    cancer
};

enum class Sector : uint8_t {
    unknown,

    urban,

    rural
};

enum class Income : uint8_t {
    unknown,

    low,

    lowermiddle,

    middle,

    uppermiddle,

    high
};

// Note: Region and Ethnicity are now dynamic string-based identifiers
// They are read from CSV files and can support any number of categories
// The system will automatically adapt to the CSV structure

template <typename T>
concept Numerical = std::is_arithmetic_v<T>;

struct LookupGenderValue {
    int value{};

    double male{};

    double female{};
};

class DataTable;
class DataTableColumn;

class StringDataTableColumn;
class FloatDataTableColumn;
class DoubleDataTableColumn;
class IntegerDataTableColumn;

class DataTableColumnVisitor;

} // namespace hgps::core
