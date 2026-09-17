// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/math_util.h, math_util.cpp.
#pragma once

namespace hgps::core {

/// @brief Floating-point representation parameters and relative comparison.
///
/// References:
///  - William Cody, Algorithm 665: MACHAR, ACM TOMS 14(4), December 1988, 303-311.
///  - Didier H. Besset, Object-Oriented Implementation of Numerical Methods, 2000.
///
/// The values are computed once on first use and cached. Unlike the baseline's version the cache
/// is initialised eagerly by a function-local static, so two threads asking at once cannot both
/// run the computation.
class MathHelper {
  public:
    MathHelper() = delete;

    static int radix() noexcept;
    static double machine_precision() noexcept;
    static double default_numerical_precision() noexcept;

    /// @brief Relative equality at the default precision.
    static bool equal(double left, double right) noexcept;

    /// @brief Relative equality: |a-b| / max(|a|,|b|) < precision, with an absolute fallback when
    ///        both values are smaller than the precision.
    static bool equal(double left, double right, double precision) noexcept;
};

} // namespace hgps::core
