// Ported from the baseline's src/HealthGPS.Tests/IncomeCategoryLayout.Test.cpp.
//
// The baseline threw core::HgpsException for an unsupported category count; core here has no
// dependency on the diagnostics module, so its precondition failures are standard exceptions —
// std::invalid_argument. The intent of the test is unchanged.
#include "core/income_category_layout.h"

#include <gtest/gtest.h>

#include <stdexcept>

TEST(IncomeCategoryLayout, ParsesThreeFourAndFive) {
    using namespace hgps::core;

    const auto layout3 = income_category_layout_from_config("3");
    EXPECT_EQ(3U, layout3.count);
    EXPECT_EQ(3U, layout3.strata.size());

    const auto layout4 = income_category_layout_from_config("4");
    EXPECT_EQ(4U, layout4.count);
    EXPECT_EQ(Income::lowermiddle, layout4.strata[1]);

    const auto layout5 = income_category_layout_from_config("5");
    EXPECT_EQ(5U, layout5.count);
    EXPECT_EQ(Income::middle, layout5.strata[2]);
}

TEST(IncomeCategoryLayout, RejectsInvalidCategoryCount) {
    EXPECT_THROW(hgps::core::income_category_layout_from_config("2"), std::invalid_argument);
    EXPECT_THROW(hgps::core::income_category_layout_from_config(""), std::invalid_argument);
    EXPECT_THROW(hgps::core::income_category_layout_from_config("three"), std::invalid_argument);
}

TEST(IncomeCategoryLayout, TableIndexAndBucketMapping) {
    using namespace hgps::core;

    const auto layout5 = income_category_layout_from_config("5");
    EXPECT_EQ(0U, income_table_index(Income::low, layout5));
    EXPECT_EQ(4U, income_table_index(Income::high, layout5));
    EXPECT_EQ(Income::uppermiddle, income_from_equal_split_bucket(3U, layout5));

    // A bucket past the top clamps rather than reading out of bounds.
    EXPECT_EQ(Income::high, income_from_equal_split_bucket(99U, layout5));

    const auto layout3 = income_category_layout_from_config("3");
    EXPECT_THROW(income_table_index(Income::lowermiddle, layout3), std::invalid_argument);
}

TEST(IncomeCategoryLayout, NumericEncodingPreservesLegacyAndSupportsFive) {
    using namespace hgps::core;

    const auto layout3 = income_category_layout_from_config("3");
    EXPECT_DOUBLE_EQ(1.0, income_category_numeric(Income::low, layout3));
    EXPECT_DOUBLE_EQ(2.0, income_category_numeric(Income::middle, layout3));
    EXPECT_DOUBLE_EQ(4.0, income_category_numeric(Income::high, layout3));

    const auto layout4 = income_category_layout_from_config("4");
    EXPECT_DOUBLE_EQ(3.0, income_category_numeric(Income::uppermiddle, layout4));

    const auto layout5 = income_category_layout_from_config("5");
    EXPECT_DOUBLE_EQ(1.0, income_category_numeric(Income::low, layout5));
    EXPECT_DOUBLE_EQ(5.0, income_category_numeric(Income::high, layout5));
}

// Added here: the layout's stratum order is the sampling order for income assignment, so it is
// part of the model definition and a test should pin it (audit B-05 / determinism clause D4).
TEST(IncomeCategoryLayout, StrataAreOrderedLowToHigh) {
    using namespace hgps::core;

    for (const auto *categories : {"3", "4", "5"}) {
        const auto layout = income_category_layout_from_config(categories);
        ASSERT_EQ(layout.count, layout.strata.size());
        ASSERT_EQ(layout.count, layout.labels.size());
        EXPECT_EQ(Income::low, layout.strata.front());
        EXPECT_EQ(Income::high, layout.strata.back());

        for (std::size_t i = 1; i < layout.strata.size(); ++i) {
            EXPECT_LT(static_cast<int>(layout.strata[i - 1]), static_cast<int>(layout.strata[i]))
                << "income strata must be ordered from lowest to highest for " << categories
                << " categories";
        }
    }
}
