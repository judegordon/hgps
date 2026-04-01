#pragma once
#include <type_traits>

namespace hgps::core {

template <typename T>
concept Numerical = std::is_arithmetic_v<T>;

} // namespace hgps::core