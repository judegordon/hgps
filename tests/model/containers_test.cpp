// Ports the baseline's src/HealthGPS.Tests/AgeGenderTable.Test.cpp (12), Map2d.Test.cpp (6),
// HierarchicalMapping.Test.cpp (8), LifeTable.Test.cpp (5), DataSeries.Test.cpp (3) and
// RuntimeMetric.Test.cpp (5), preserving their properties and values.
//
// Two API differences carried over from docs/deviations.md: Map2d is always ordered (the
// baseline also offers an unordered variant, and anything iterating one of these is producing a
// result), and RuntimeMetric is ordered for the same reason.
#include "model/containers.h"
#include "model/life_table.h"
#include "model/mapping.h"
#include "model/results.h"

#include "diagnostics/internal_error.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using hgps::core::DoubleArray2D;
using hgps::core::Gender;
using hgps::core::Identifier;
using hgps::core::IntegerInterval;
using hgps::core::Income;
using namespace hgps::model;

TEST(TestHealthGPS_AgeGenderTable, CreateEmpty) {
    const auto table = DoubleAgeGenderTable();

    EXPECT_EQ(0U, table.size());
    EXPECT_EQ(0U, table.rows());
    EXPECT_EQ(0U, table.columns());
    EXPECT_TRUE(table.empty());
    EXPECT_FALSE(table.contains(0));
}

TEST(TestHealthGPS_AgeGenderTable, CreateBlank) {
    const MonotonicVector<int> rows{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
    const std::vector<Gender> columns{Gender::male, Gender::female};
    const auto table =
        DoubleAgeGenderTable(rows, columns, DoubleArray2D(rows.size(), columns.size()));

    EXPECT_EQ(rows.size() * columns.size(), table.size());
    EXPECT_EQ(rows.size(), table.rows());
    EXPECT_EQ(columns.size(), table.columns());
    EXPECT_FALSE(table.empty());
    EXPECT_TRUE(table.contains(0));
    EXPECT_TRUE(table.contains(10));
    EXPECT_FALSE(table.contains(13));
}

TEST(TestHealthGPS_AgeGenderTable, CreateWithSizeMismatchThrows) {
    const MonotonicVector<int> rows{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
    const std::vector<Gender> columns{Gender::male, Gender::female};

    EXPECT_THROW(DoubleAgeGenderTable(rows, columns, DoubleArray2D(rows.size() - 3, 2)),
                 std::invalid_argument);
    EXPECT_THROW(DoubleAgeGenderTable(rows, columns, DoubleArray2D(rows.size(), 1)),
                 std::invalid_argument);
}

TEST(TestHealthGPS_AgeGenderTable, CreateBlankFunction) {
    const auto age_range = IntegerInterval(0, 10);
    const auto table = create_age_gender_table<double>(age_range);

    EXPECT_EQ(22U, table.size());
    EXPECT_EQ(11U, table.rows());
    EXPECT_EQ(2U, table.columns());
    EXPECT_FALSE(table.empty());
    EXPECT_TRUE(table.contains(5));
    EXPECT_FALSE(table.contains(13));
}

TEST(TestHealthGPS_AgeGenderTable, AccessContainsCheck) {
    const auto table = create_age_gender_table<double>(IntegerInterval(0, 10));

    for (int age = 0; age <= 10; ++age) {
        EXPECT_TRUE(table.contains(age));
        EXPECT_TRUE(table.contains(age, Gender::male));
        EXPECT_TRUE(table.contains(age, Gender::female));
        EXPECT_FALSE(table.contains(age, Gender::unknown));
    }

    EXPECT_FALSE(table.contains(13, Gender::male));
}

TEST(TestHealthGPS_AgeGenderTable, AccessViaIndex) {
    auto table = create_age_gender_table<double>(IntegerInterval(0, 10));

    for (int age = 0; age <= 10; ++age) {
        EXPECT_DOUBLE_EQ(0.0, table(age, Gender::male));
        EXPECT_DOUBLE_EQ(0.0, table.at(age, Gender::female));
    }
}

TEST(TestHealthGPS_AgeGenderTable, AccessViaConstIndex) {
    const auto table = create_age_gender_table<double>(IntegerInterval(0, 10));

    for (int age = 0; age <= 10; ++age) {
        EXPECT_DOUBLE_EQ(0.0, table(age, Gender::male));
        EXPECT_DOUBLE_EQ(0.0, table.at(age, Gender::female));
    }
}

TEST(TestHealthGPS_AgeGenderTable, UpdateViaOperatorAndFunction) {
    auto table = create_age_gender_table<double>(IntegerInterval(0, 10));

    for (int age = 0; age <= 10; ++age) {
        table(age, Gender::male) = 5.0 + age;
        table.at(age, Gender::female) = 3.5 + age;
    }

    for (int age = 0; age <= 10; ++age) {
        EXPECT_DOUBLE_EQ(5.0 + age, table(age, Gender::male));
        EXPECT_DOUBLE_EQ(3.5 + age, table.at(age, Gender::female));
    }
}

TEST(TestHealthGPS_AgeGenderTable, CreateWithWrongRangeThrows) {
    EXPECT_THROW(create_age_gender_table<double>(IntegerInterval(-1, 10)), std::invalid_argument);
    EXPECT_THROW(create_age_gender_table<double>(IntegerInterval(1, 1)), std::invalid_argument);
    EXPECT_THROW(create_integer_gender_table<int>(IntegerInterval(-1, 10)), std::out_of_range);
    EXPECT_THROW(create_integer_gender_table<int>(IntegerInterval(5, 5)), std::out_of_range);
}

TEST(TestHealthGPS_AgeGenderTable, AccessEmptyThrows) {
    auto table = DoubleAgeGenderTable();
    EXPECT_THROW(table(0, Gender::male), std::out_of_range);
    EXPECT_THROW(table.at(0, Gender::male), std::out_of_range);
}

TEST(TestHealthGPS_AgeGenderTable, AccessOutOfRangeThrows) {
    auto table = create_age_gender_table<double>(IntegerInterval(0, 10));

    EXPECT_THROW(table(13, Gender::male), std::out_of_range);
    EXPECT_THROW(table.at(0, Gender::unknown), std::out_of_range);
}

TEST(TestHealthGPS_AgeGenderTable, RowKeysAreOrdered) {
    // Added here: the row keys are what an iterating caller sees, so their order is part of the
    // contract rather than an accident of the index container.
    const auto table = create_age_gender_table<double>(IntegerInterval(0, 4));
    EXPECT_EQ(std::vector<int>({0, 1, 2, 3, 4}), table.row_keys());
}

TEST(TestHealthGPS_MonotonicVector, RejectsNonMonotonicValues) {
    EXPECT_NO_THROW(MonotonicVector<int>(std::vector<int>{1, 2, 3}));
    EXPECT_NO_THROW(MonotonicVector<int>(std::vector<int>{3, 2, 1}));
    EXPECT_NO_THROW(MonotonicVector<int>(std::vector<int>{}));
    EXPECT_NO_THROW(MonotonicVector<int>(std::vector<int>{7}));

    EXPECT_THROW(MonotonicVector<int>(std::vector<int>{1, 1, 2}), std::invalid_argument);
    EXPECT_THROW(MonotonicVector<int>(std::vector<int>{1, 3, 2}), std::invalid_argument);
    EXPECT_THROW(MonotonicVector<double>(std::vector<double>{1.0, 0.5, 2.0}),
                 std::invalid_argument);
}

TEST(TestHealthGPS_Map2d, CreateEmptyAndInsert) {
    OrderedMap2d<std::string, int, double> table;

    EXPECT_TRUE(table.empty());
    EXPECT_EQ(0U, table.rows());
    EXPECT_FALSE(table.contains("bmi"));

    table.emplace("bmi", 1, 2.5);
    table.emplace("bmi", 2, 3.5);
    table.emplace("age", 1, 1.5);

    EXPECT_FALSE(table.empty());
    EXPECT_EQ(2U, table.rows());
    EXPECT_EQ(2U, table.columns("bmi"));
    EXPECT_TRUE(table.contains("bmi"));
    EXPECT_TRUE(table.contains("bmi", 2));
    EXPECT_FALSE(table.contains("bmi", 3));
    EXPECT_FALSE(table.contains("sodium", 1));
    EXPECT_DOUBLE_EQ(3.5, table.at("bmi", 2));
}

TEST(TestHealthGPS_Map2d, AccessOutOfRangeThrows) {
    OrderedMap2d<std::string, int, double> table;
    table.emplace("bmi", 1, 2.5);

    EXPECT_THROW(table.at("sodium"), std::out_of_range);
    EXPECT_THROW(table.at("bmi", 9), std::out_of_range);
    EXPECT_THROW(table.empty("sodium"), std::out_of_range);
    EXPECT_THROW(table.columns("sodium"), std::out_of_range);
}

TEST(TestHealthGPS_Map2d, IteratesInKeyOrder) {
    OrderedMap2d<std::string, int, double> table;
    table.emplace("sodium", 1, 1.0);
    table.emplace("bmi", 1, 2.0);
    table.emplace("energy", 1, 3.0);

    std::vector<std::string> keys;
    for (const auto &[row, columns] : table) {
        keys.push_back(row);
    }

    EXPECT_EQ(std::vector<std::string>({"bmi", "energy", "sodium"}), keys);
}

TEST(TestHealthGPS_Map2d, UpdateAndReplaceCell) {
    OrderedMap2d<std::string, int, double> table;
    table.emplace("bmi", 1, 2.5);
    table.at("bmi", 1) = 4.0;
    EXPECT_DOUBLE_EQ(4.0, table.at("bmi", 1));

    table.emplace("bmi", 1, 5.0);
    EXPECT_DOUBLE_EQ(5.0, table.at("bmi", 1));
    EXPECT_EQ(1U, table.columns("bmi"));
}

TEST(TestHealthGPS_Map2d, EmplaceWholeRow) {
    OrderedMap2d<std::string, int, double> table;
    table.emplace_row("bmi", {{1, 1.0}, {2, 2.0}});

    EXPECT_EQ(1U, table.rows());
    EXPECT_EQ(2U, table.columns("bmi"));
    EXPECT_FALSE(table.empty("bmi"));
}

TEST(TestHealthGPS_Mapping, CreateEntry) {
    const MappingEntry entry{"BMI", 3, hgps::core::DoubleInterval{15.0, 40.0}};

    EXPECT_EQ("BMI", entry.name());
    EXPECT_EQ(Identifier{"bmi"}, entry.key());
    EXPECT_EQ(3, entry.level());
    ASSERT_TRUE(entry.range().has_value());
    EXPECT_DOUBLE_EQ(15.0, entry.range()->lower());
}

TEST(TestHealthGPS_Mapping, BoundedValueClampsOnlyWhenARangeIsGiven) {
    const MappingEntry bounded{"BMI", 3, hgps::core::DoubleInterval{15.0, 40.0}};
    EXPECT_DOUBLE_EQ(15.0, bounded.get_bounded_value(10.0));
    EXPECT_DOUBLE_EQ(40.0, bounded.get_bounded_value(50.0));
    EXPECT_DOUBLE_EQ(24.0, bounded.get_bounded_value(24.0));

    const MappingEntry unbounded{"SES", 0};
    EXPECT_FALSE(unbounded.range().has_value());
    EXPECT_DOUBLE_EQ(-99.0, unbounded.get_bounded_value(-99.0));
}

TEST(TestHealthGPS_Mapping, HierarchicalMappingLevelsAndLookup) {
    const HierarchicalMapping mapping{{MappingEntry{"Gender", 0}, MappingEntry{"Age", 0},
                                       MappingEntry{"Sodium", 1}, MappingEntry{"BMI", 3}}};

    EXPECT_EQ(4U, mapping.size());
    EXPECT_EQ(3, mapping.max_level());
    EXPECT_TRUE(mapping.contains(Identifier{"bmi"}));
    EXPECT_FALSE(mapping.contains(Identifier{"energy"}));
    EXPECT_EQ(3, mapping.at(Identifier{"bmi"}).level());
    EXPECT_THROW(mapping.at(Identifier{"energy"}), std::out_of_range);

    const auto level0 = mapping.at_level(0);
    ASSERT_EQ(2U, level0.size());
    // Declaration order within a level, because that is the order factors are generated in and
    // therefore the order RNG draws are taken in.
    EXPECT_EQ("Gender", level0[0].name());
    EXPECT_EQ("Age", level0[1].name());

    EXPECT_TRUE(mapping.at_level(2).empty());
}

TEST(TestHealthGPS_Mapping, KeysAreInDeclarationOrder) {
    const HierarchicalMapping mapping{
        {MappingEntry{"Sodium", 1}, MappingEntry{"BMI", 3}, MappingEntry{"Age", 0}}};

    const auto keys = mapping.keys();
    ASSERT_EQ(3U, keys.size());
    EXPECT_EQ(Identifier{"sodium"}, keys[0]);
    EXPECT_EQ(Identifier{"bmi"}, keys[1]);
    EXPECT_EQ(Identifier{"age"}, keys[2]);
}

TEST(TestHealthGPS_Mapping, EmptyMappingHasNoLevels) {
    const HierarchicalMapping mapping{{}};
    EXPECT_EQ(0U, mapping.size());
    EXPECT_EQ(0, mapping.max_level());
    EXPECT_TRUE(mapping.keys().empty());
}

TEST(TestHealthGPS_LifeTable, CreateAndQuery) {
    std::map<int, Birth> births{{2010, Birth{1000.0F, 105.0F}}, {2011, Birth{1010.0F, 105.0F}}};
    std::map<int, std::map<int, Mortality>> deaths{
        {2010, {{0, Mortality{1.0F, 2.0F}}, {1, Mortality{3.0F, 4.0F}}}},
        {2011, {{0, Mortality{1.5F, 2.5F}}, {1, Mortality{3.5F, 4.5F}}}}};

    const LifeTable table{births, deaths};

    EXPECT_FALSE(table.empty());
    EXPECT_EQ(IntegerInterval(2010, 2011), table.time_limits());
    EXPECT_EQ(IntegerInterval(0, 1), table.age_limits());
    EXPECT_TRUE(table.contains_time(2010));
    EXPECT_FALSE(table.contains_time(2012));
    EXPECT_TRUE(table.contains_age(1));
    EXPECT_FALSE(table.contains_age(2));

    EXPECT_FLOAT_EQ(1000.0F, table.get_births_at(2010).number);
    EXPECT_FLOAT_EQ(105.0F, table.get_births_at(2010).sex_ratio);
    EXPECT_EQ(2U, table.get_mortalities_at(2010).size());
    EXPECT_DOUBLE_EQ(10.0, table.get_total_deaths_at(2010));
}

TEST(TestHealthGPS_LifeTable, QueryUnknownYearThrows) {
    const LifeTable table{{{2010, Birth{1.0F, 105.0F}}}, {{2010, {{0, Mortality{1.0F, 1.0F}}}}}};

    EXPECT_THROW(table.get_births_at(1999), std::out_of_range);
    EXPECT_THROW(table.get_mortalities_at(1999), std::out_of_range);
    EXPECT_THROW(table.get_total_deaths_at(1999), std::out_of_range);
}

TEST(TestHealthGPS_LifeTable, EmptyTable) {
    const LifeTable table{{}, {}};
    EXPECT_TRUE(table.empty());
    EXPECT_FALSE(table.contains_time(2010));
}

TEST(TestHealthGPS_LifeTable, TotalDeathsSumsAgesInOrder) {
    // Ordered summation, so the total is reproducible: the map is keyed by age.
    std::map<int, Mortality> by_age;
    for (int age = 0; age <= 100; ++age) {
        by_age[age] = Mortality{1.0F / static_cast<float>(age + 1), 0.5F};
    }

    const LifeTable table{{{2010, Birth{1.0F, 105.0F}}}, {{2010, by_age}}};

    double expected = 0.0;
    for (const auto &[age, mortality] : by_age) {
        expected += static_cast<double>(mortality.male) + static_cast<double>(mortality.female);
    }

    EXPECT_DOUBLE_EQ(expected, table.get_total_deaths_at(2010));
}

TEST(TestHealthGPS_DataSeries, AddChannelsAndAccess) {
    DataSeries series{5};

    EXPECT_EQ(5U, series.size());
    EXPECT_TRUE(series.channels().empty());

    series.add_channel("bmi");
    series.add_channels({"energy", "sodium"});

    EXPECT_EQ(std::vector<std::string>({"bmi", "energy", "sodium"}), series.channels());
    EXPECT_TRUE(series.contains("bmi"));
    EXPECT_FALSE(series.contains("weight"));

    EXPECT_EQ(5U, series.at(Gender::male, "bmi").size());
    series(Gender::male, "bmi")[2] = 24.5;
    EXPECT_DOUBLE_EQ(24.5, series.at(Gender::male, "bmi")[2]);
    EXPECT_DOUBLE_EQ(0.0, series.at(Gender::female, "bmi")[2]);
}

TEST(TestHealthGPS_DataSeries, DuplicateChannelThrows) {
    DataSeries series{3};
    series.add_channel("bmi");
    EXPECT_THROW(series.add_channel("bmi"), std::logic_error);
}

TEST(TestHealthGPS_DataSeries, UnknownChannelThrows) {
    DataSeries series{3};
    EXPECT_THROW(series.at(Gender::male, "nope"), std::out_of_range);
    EXPECT_THROW(series.at(Gender::male, Income::low, "nope"), std::out_of_range);
}

TEST(TestHealthGPS_DataSeries, IncomeChannelsAreCreatedOnFirstUse) {
    DataSeries series{4};
    series.add_channel("bmi");

    EXPECT_FALSE(series.has_income_channels());

    series.at(Gender::male, Income::low, "bmi")[1] = 22.0;

    EXPECT_TRUE(series.has_income_channels());
    EXPECT_EQ(4U, series.at(Gender::male, Income::low, "bmi").size());
    EXPECT_DOUBLE_EQ(22.0, series.at(Gender::male, Income::low, "bmi")[1]);
    EXPECT_DOUBLE_EQ(0.0, series.at(Gender::female, Income::low, "bmi")[1]);
}

TEST(TestHealthGPS_Metrics, EmplaceAtAndErase) {
    RuntimeMetric metrics;

    EXPECT_TRUE(metrics.empty());
    EXPECT_TRUE(metrics.emplace("simulated_time", 1.5));
    EXPECT_FALSE(metrics.emplace("simulated_time", 2.5));
    EXPECT_EQ(1U, metrics.size());
    EXPECT_DOUBLE_EQ(1.5, metrics.at("simulated_time"));
    EXPECT_TRUE(metrics.contains("simulated_time"));

    metrics["another"] = 3.0;
    EXPECT_EQ(2U, metrics.size());

    EXPECT_TRUE(metrics.erase("another"));
    EXPECT_FALSE(metrics.erase("another"));
    EXPECT_THROW(metrics.at("another"), std::out_of_range);
}

TEST(TestHealthGPS_Metrics, ResetKeepsKeysAndClearRemovesThem) {
    RuntimeMetric metrics;
    metrics["a"] = 1.0;
    metrics["b"] = 2.0;

    metrics.reset();
    EXPECT_EQ(2U, metrics.size());
    EXPECT_DOUBLE_EQ(0.0, metrics.at("a"));

    metrics.clear();
    EXPECT_TRUE(metrics.empty());
}

TEST(TestHealthGPS_Metrics, IteratesInKeyOrder) {
    RuntimeMetric metrics;
    metrics["zulu"] = 1.0;
    metrics["alpha"] = 2.0;
    metrics["mike"] = 3.0;

    std::vector<std::string> keys;
    for (const auto &[key, value] : metrics) {
        keys.push_back(key);
    }

    EXPECT_EQ(std::vector<std::string>({"alpha", "mike", "zulu"}), keys);
}

TEST(TestHealthGPS_Metrics, ResultsByGenderAndIncome) {
    ResultByGender by_gender{.male = 1.0, .female = 2.0};
    EXPECT_DOUBLE_EQ(1.0, by_gender.at(Gender::male));
    by_gender.at(Gender::female) = 3.0;
    EXPECT_DOUBLE_EQ(3.0, by_gender.female);

    ResultByIncome by_income;
    by_income.at(Income::middle) = 5.0;
    EXPECT_DOUBLE_EQ(5.0, by_income.middle);
    EXPECT_THROW(by_income.at(Income::unknown), hgps::diag::InternalError);

    ResultByIncomeGender by_both;
    by_both.at(Income::high).male = 7.0;
    EXPECT_DOUBLE_EQ(7.0, by_both.high.male);
    EXPECT_THROW(by_both.at(Income::unknown), hgps::diag::InternalError);
}

TEST(TestHealthGPS_Metrics, ModelResultRenders) {
    ModelResult result{3};
    result.population_size = 100;
    result.number_alive = {.male = 48, .female = 50};
    result.number_dead = 2;
    result.risk_factor_average["bmi"] = {.male = 25.5, .female = 24.5};

    const auto text = result.to_string();
    EXPECT_NE(std::string::npos, text.find("Population size"));
    EXPECT_NE(std::string::npos, text.find("bmi"));
}
