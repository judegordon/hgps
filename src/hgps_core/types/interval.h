#pragma once

#include "diagnostics/internal_error.h"
#include "../forward_type.h"
#include "utils/string_util.h"

#include <algorithm>
#include <fmt/format.h>

namespace hgps::core {

template <Numerical TYPE> class Interval {
  public:
    Interval() = default;

    explicit Interval(TYPE lower_value, TYPE upper_value)
        : lower_{lower_value}, upper_{upper_value} {
        if (lower_ > upper_) {
            throw InternalError(fmt::format("Invalid interval: {}-{}", lower_, upper_));
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

    std::string to_string() const { return fmt::format("{}-{}", lower_, upper_); }

    auto operator<=>(const Interval<TYPE> &rhs) const = default;

  private:
    TYPE lower_{};
    TYPE upper_{};
};

using IntegerInterval = Interval<int>;

using FloatInterval = Interval<float>;

using DoubleInterval = Interval<double>;

IntegerInterval parse_integer_interval(const std::string_view &value,
                                       const std::string_view delims = "-");

FloatInterval parse_float_interval(const std::string_view &value,
                                   const std::string_view delims = "-");

DoubleInterval parse_double_interval(const std::string_view &value,
                                     const std::string_view delims = "-");
} // namespace hgps::core
