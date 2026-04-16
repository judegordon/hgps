#pragma once

#include <cstdint>
#include <type_traits>

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

struct LookupGenderValue {
    int value{};
    double male{};
    double female{};
};

class DataTable;
class DataTableColumn;

} // namespace hgps::core