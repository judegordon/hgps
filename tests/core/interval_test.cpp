// Ported from the baseline's src/HealthGPS.Tests/Interval.Test.cpp.
#include "core/interval.h"

#include <gtest/gtest.h>

TEST(TestCore_Interval, CreateEmpty) {
    using namespace hgps::core;
    const auto empty = IntegerInterval{};

    ASSERT_EQ(0, empty.length());
    ASSERT_EQ(0, empty.lower());
    ASSERT_EQ(0, empty.upper());
    ASSERT_TRUE(empty.contains(0));
}

TEST(TestCore_Interval, CreatePositive) {
    using namespace hgps::core;
    const auto lower = 0;
    const auto upper = 10;
    const auto len = upper - lower;
    const auto mid = len / 2;
    const auto animal = IntegerInterval{lower, upper};
    const auto cat = IntegerInterval{mid, upper};
    const auto dog = IntegerInterval{lower, mid};

    ASSERT_EQ(lower, animal.lower());
    ASSERT_EQ(upper, animal.upper());
    ASSERT_EQ(len, animal.length());
    ASSERT_TRUE(animal.contains(mid));
    ASSERT_TRUE(animal.contains(cat));
    ASSERT_TRUE(animal.contains(dog));
}

TEST(TestCore_Interval, Comparable) {
    using namespace hgps::core;

    const auto lower = 0;
    const auto upper = 10;
    const auto len = upper - lower;
    const auto mid = len / 2;

    const auto cat = IntegerInterval{lower, upper};
    const auto gato = IntegerInterval{lower, upper};
    const auto dog = IntegerInterval{upper, upper + mid};
    const auto cow = IntegerInterval{lower - mid, lower};

    ASSERT_EQ(cat, gato);
    ASSERT_GT(dog, cat);
    ASSERT_LT(cow, dog);

    ASSERT_TRUE(dog > cat);
    ASSERT_TRUE(cow < dog);
    ASSERT_TRUE(dog >= cat);
    ASSERT_TRUE(cow <= dog);

    ASSERT_TRUE(cat >= cow);
    ASSERT_TRUE(dog >= cat);
    ASSERT_FALSE(cat > dog);
    ASSERT_FALSE(dog < cow);
}

TEST(TestCore_Interval, ParseInteger) {
    using namespace hgps::core;
    const auto animal = IntegerInterval{0, 10};
    ASSERT_EQ(animal, parse_integer_interval(animal.to_string()));
    ASSERT_THROW(parse_integer_interval("TheFox"), std::invalid_argument);
}

TEST(TestCore_Interval, ParseFloat) {
    using namespace hgps::core;
    const auto animal = FloatInterval{0.0F, 10.0F};
    ASSERT_EQ(animal, parse_float_interval(animal.to_string()));
    ASSERT_THROW(parse_float_interval("TheFox"), std::invalid_argument);
}

TEST(TestCore_Interval, ParseDouble) {
    using namespace hgps::core;
    const auto animal = DoubleInterval{0.0, 10.0};
    ASSERT_EQ(animal, parse_double_interval(animal.to_string()));
    ASSERT_THROW(parse_double_interval("TheFox"), std::invalid_argument);
}

TEST(TestCore_Interval, IntegerIntervalContainsBoundary) {
    using namespace hgps::core;
    const IntegerInterval iv(5, 10);
    ASSERT_TRUE(iv.contains(5));
    ASSERT_TRUE(iv.contains(10));
    ASSERT_FALSE(iv.contains(4));
    ASSERT_FALSE(iv.contains(11));
}

TEST(TestCore_Interval, IntegerIntervalSinglePoint) {
    using namespace hgps::core;
    const IntegerInterval iv(7, 7);
    ASSERT_EQ(0, iv.length());
    ASSERT_TRUE(iv.contains(7));
}

TEST(TestCore_Interval, DoubleIntervalContains) {
    using namespace hgps::core;
    const DoubleInterval iv(18.0, 65.0);
    ASSERT_TRUE(iv.contains(18.0));
    ASSERT_TRUE(iv.contains(65.0));
    ASSERT_TRUE(iv.contains(40.0));
    ASSERT_FALSE(iv.contains(17.9));
}

TEST(TestCore_Interval, FloatIntervalLength) {
    using namespace hgps::core;
    const FloatInterval iv(0.0F, 10.0F);
    ASSERT_FLOAT_EQ(10.0F, iv.length());
}

TEST(TestCore_Interval, InvertedBoundsThrow) {
    using namespace hgps::core;
    ASSERT_THROW(IntegerInterval(10, 5), std::invalid_argument);
    ASSERT_THROW(DoubleInterval(1.5, -1.5), std::invalid_argument);
}

// Added here: the baseline parsed intervals by calling std::stoi on a string_view's .data(),
// which is not null-terminated at the field boundary. It happened to work because stoi stops at
// the '-'; a delimiter that is not a stoi terminator would have read past the field.
TEST(TestCore_Interval, ParseWithNonNumericDelimiter) {
    using namespace hgps::core;
    ASSERT_EQ(IntegerInterval(3, 9), parse_integer_interval("3 to 9", "to "));
    ASSERT_EQ(DoubleInterval(0.5, 2.5), parse_double_interval("0.5:2.5", ":"));
}
