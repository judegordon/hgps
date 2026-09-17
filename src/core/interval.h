// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/interval.h, interval.cpp.
#pragma once

#include "types.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace hgps::core {

/// @brief A closed numeric interval [lower, upper].
template <Numerical TYPE> class Interval {
  public:
    Interval() = default;

    /// @throws std::invalid_argument if lower > upper.
    explicit Interval(TYPE lower_value, TYPE upper_value)
        : lower_{lower_value}, upper_{upper_value} {
        if (lower_ > upper_) {
            throw std::invalid_argument(fmt::format("Invalid interval: {}-{}", lower_, upper_));
        }
    }

    TYPE lower() const noexcept { return lower_; }
    TYPE upper() const noexcept { return upper_; }
    TYPE length() const noexcept { return upper_ - lower_; }

    bool contains(TYPE value) const noexcept { return lower_ <= value && value <= upper_; }

    bool contains(const Interval<TYPE> &other) const noexcept {
        return contains(other.lower_) && contains(other.upper_);
    }

    TYPE clamp(TYPE value) const noexcept { return std::clamp(value, lower_, upper_); }

    std::string to_string() const noexcept { return fmt::format("{}-{}", lower_, upper_); }

    auto operator<=>(const Interval<TYPE> &rhs) const = default;
    bool operator==(const Interval<TYPE> &rhs) const = default;

  private:
    TYPE lower_{};
    TYPE upper_{};
};

using IntegerInterval = Interval<int>;
using FloatInterval = Interval<float>;
using DoubleInterval = Interval<double>;

/// @throws std::invalid_argument for a string that is not two numbers separated by a delimiter.
IntegerInterval parse_integer_interval(std::string_view value, std::string_view delims = "-");
FloatInterval parse_float_interval(std::string_view value, std::string_view delims = "-");
DoubleInterval parse_double_interval(std::string_view value, std::string_view delims = "-");

} // namespace hgps::core
