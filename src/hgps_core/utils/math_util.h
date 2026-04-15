#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace hgps::core {

class MathHelper {
  public:
    MathHelper() = delete;

    static int radix() noexcept { return std::numeric_limits<double>::radix; }

    static double machine_precision() noexcept { return std::numeric_limits<double>::epsilon(); }

    static double default_numerical_precision() noexcept {
        return std::sqrt(machine_precision());
    }

    static bool equal(double left, double right) noexcept {
        return equal(left, right, default_numerical_precision());
    }

    static bool equal(double left, double right, double precision) noexcept {
        const double norm = std::max(std::abs(left), std::abs(right));
        return norm < precision || std::abs(left - right) < precision * norm;
    }
};

} // namespace hgps::core