#pragma once
#include <cstdint>

namespace hgps {

enum class WeightCategory : uint8_t {
    unknown = 0,

    normal,

    overweight,

    obese
};

} // namespace hgps