// Ported from the baseline's src/HealthGPS.Tests/Core.MathHelper.Test.cpp.
#include "core/math_util.h"

#include <gtest/gtest.h>

#include <limits>

TEST(TestCore_MathHelper, RadixValue) {
    using namespace hgps::core;

    const auto radix = MathHelper::radix();
    ASSERT_LT(0, radix);
    ASSERT_EQ(2, radix);
}

TEST(TestCore_MathHelper, MachinePrecision) {
    using namespace hgps::core;
    ASSERT_LT(0.0, MathHelper::machine_precision());
}

TEST(TestCore_MathHelper, NumericalPrecision) {
    using namespace hgps::core;
    ASSERT_LT(0.0, MathHelper::default_numerical_precision());
}

TEST(TestCore_MathHelper, EqualsDefaultPrecision) {
    using namespace hgps::core;
    const double a = (0.3 * 3.0) + 0.1;
    const double b = 1.0;
    ASSERT_TRUE(MathHelper::equal(a, b));
}

TEST(TestCore_MathHelper, EqualsEpsilonPrecision) {
    using namespace hgps::core;
    const auto a = 1.0;
    constexpr auto b = 1.0 + std::numeric_limits<double>::epsilon();
    ASSERT_TRUE(MathHelper::equal(a, b));
}

TEST(TestCore_MathHelper, EqualsCustomPrecision) {
    using namespace hgps::core;
    const double a = (0.3 * 3.0) + 0.1;
    const double b = 1.0;
    ASSERT_TRUE(MathHelper::equal(a, b, 1e-10));
}

TEST(TestCore_MathHelper, EqualsZeroPrecision) {
    using namespace hgps::core;
    ASSERT_TRUE(MathHelper::equal(0.0, 1e-15, 1e-10));
}

TEST(TestCore_MathHelper, UnequalValuesAreNotEqual) {
    using namespace hgps::core;
    ASSERT_FALSE(MathHelper::equal(1.0, 1.001));
    ASSERT_FALSE(MathHelper::equal(0.0, 1.0));
}
