#include "math_util.h"

#include <algorithm>
#include <cmath>

namespace hgps::core {
namespace {

int compute_radix() noexcept {
    auto a = 1.0;
    double tmp1 = 0.0;
    double tmp2 = 0.0;
    do {
        a += a;
        tmp1 = a + 1.0;
        tmp2 = tmp1 - a;
    } while (tmp2 - 1.0 != 0.0);

    auto b = 1.0;
    int radix = 0;
    while (radix == 0) {
        b += b;
        tmp1 = a + b;
        radix = static_cast<int>(tmp1 - a);
    }

    return radix;
}

double compute_machine_precision(int radix) noexcept {
    const auto inverse_radix = 1.0 / static_cast<double>(radix);

    auto precision = 1.0;
    auto local = 1.0 + precision;
    while (local - 1.0 != 0.0) {
        precision *= inverse_radix;
        local = 1.0 + precision;
    }

    return precision;
}

} // namespace

int MathHelper::radix() noexcept {
    static const int value = compute_radix();
    return value;
}

double MathHelper::machine_precision() noexcept {
    static const double value = compute_machine_precision(radix());
    return value;
}

double MathHelper::default_numerical_precision() noexcept {
    static const double value = std::sqrt(machine_precision());
    return value;
}

bool MathHelper::equal(double left, double right) noexcept {
    return equal(left, right, default_numerical_precision());
}

bool MathHelper::equal(double left, double right, double precision) noexcept {
    const double norm = std::max(std::abs(left), std::abs(right));
    return norm < precision || std::abs(left - right) < precision * norm;
}

} // namespace hgps::core
