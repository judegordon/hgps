#pragma once

namespace hgps::core {
class MathHelper {
  public:
    MathHelper() = delete;

    static int radix() noexcept;

    static double machine_precision() noexcept;

    static double default_numerical_precision() noexcept;

    static bool equal(double left, double right) noexcept;

    static bool equal(double left, double right, double precision) noexcept;

  private:
    static int radix_;

    static double machine_precision_;

    static double numerical_precision_;

    static void compute_radix() noexcept;

    static void compute_machine_precision() noexcept;
};
} // namespace hgps::core
