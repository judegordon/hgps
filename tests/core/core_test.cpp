// Ported from the baseline's src/HealthGPS.Tests/Core.Test.cpp.
#include "core/datatable.h"
#include "core/entities.h"
#include "core/string_util.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <vector>

TEST(TestCore, CreateCountry) {
    using namespace hgps::core;

    const int id = 826;
    const auto *uk = "United Kingdom";

    const auto c = Country{.code = id, .name = uk, .alpha2 = "GB", .alpha3 = "GBR"};

    EXPECT_EQ(id, c.code);
    EXPECT_EQ(uk, c.name);
    EXPECT_EQ("GB", c.alpha2);
    EXPECT_EQ("GBR", c.alpha3);
}

TEST(TestCore, CreateTableColumnWithNulls) {
    using namespace hgps::core;

    const auto str_col = StringDataTableColumn{"string", {"Cat", "Dog", ""}, {true, true, false}};
    const auto flt_col =
        FloatDataTableColumn("float", {5.7F, 15.37F, 0.0F, 20.75F}, {true, true, false, true});
    const auto dbl_col = DoubleDataTableColumn("double", {7.13, 15.37, 20.75}, {true, true, true});
    const auto int_col = IntegerDataTableColumn("integer", {0, 15, 200}, {false, true, true});

    ASSERT_EQ(3U, str_col.size());
    ASSERT_EQ(1U, str_col.null_count());
    ASSERT_EQ("Dog", *str_col.value_safe(1));
    ASSERT_EQ("Dog", str_col.value_unsafe(1));
    ASSERT_TRUE(str_col.is_null(2));
    ASSERT_FALSE(str_col.is_valid(2));
    ASSERT_FALSE(str_col.is_null(0));
    ASSERT_TRUE(str_col.is_valid(0));

    ASSERT_EQ(4U, flt_col.size());
    ASSERT_EQ(1U, flt_col.null_count());
    ASSERT_EQ(15.37F, *flt_col.value_safe(1));
    ASSERT_EQ(15.37F, flt_col.value_unsafe(1));
    ASSERT_TRUE(flt_col.is_null(2));
    ASSERT_FALSE(flt_col.is_valid(2));

    ASSERT_EQ(3U, dbl_col.size());
    ASSERT_EQ(0U, dbl_col.null_count());
    ASSERT_EQ(15.37, *dbl_col.value_safe(1));
    ASSERT_EQ(15.37, dbl_col.value_unsafe(1));
    ASSERT_FALSE(dbl_col.is_null(1));
    ASSERT_TRUE(dbl_col.is_valid(1));

    ASSERT_EQ(3U, int_col.size());
    ASSERT_EQ(1U, int_col.null_count());
    ASSERT_EQ(15, *int_col.value_safe(1));
    ASSERT_EQ(15, int_col.value_unsafe(1));
    ASSERT_TRUE(int_col.is_null(0));
    ASSERT_FALSE(int_col.is_valid(0));
}

TEST(TestCore, CreateTableColumnWithoutNulls) {
    using namespace hgps::core;

    const auto str_col = StringDataTableColumn("string", {"Cat", "Dog", "Cow"});
    const auto flt_col = FloatDataTableColumn("float", {7.13F, 15.37F, 0.0F, 20.75F});
    const auto dbl_col = DoubleDataTableColumn("double", {7.13, 15.37, 20.75});
    const auto int_col = IntegerDataTableColumn("integer", {0, 15, 200});

    ASSERT_EQ(3U, str_col.size());
    ASSERT_EQ(0U, str_col.null_count());
    ASSERT_EQ("Dog", *str_col.value_safe(1));
    ASSERT_TRUE(str_col.is_valid(0));
    ASSERT_FALSE(str_col.is_null(0));

    ASSERT_EQ(4U, flt_col.size());
    ASSERT_EQ(0U, flt_col.null_count());
    ASSERT_EQ(15.37F, flt_col.value_unsafe(1));

    ASSERT_EQ(3U, dbl_col.size());
    ASSERT_EQ(0U, dbl_col.null_count());
    ASSERT_EQ(15.37, dbl_col.value_unsafe(1));

    ASSERT_EQ(3U, int_col.size());
    ASSERT_EQ(0U, int_col.null_count());
    ASSERT_EQ(15, int_col.value_unsafe(1));
    ASSERT_TRUE(int_col.is_valid(0));
    ASSERT_FALSE(int_col.is_null(0));
}

TEST(TestCore, CreateTableColumnFailWithLenMismatch) {
    using namespace hgps::core;
    ASSERT_THROW(IntegerDataTableColumn("integer", {5, 15, 0, 20}, {true, false, true}),
                 std::out_of_range);
}

TEST(TestCore, CreateTableColumnFailWithShortName) {
    using namespace hgps::core;
    ASSERT_THROW(IntegerDataTableColumn("f", {15, 0, 20}, {true, false, true}),
                 std::invalid_argument);
}

TEST(TestCore, CreateTableColumnFailWithInvalidName) {
    using namespace hgps::core;
    ASSERT_THROW(IntegerDataTableColumn("5nteger", {15, 0, 20}, {true, false, true}),
                 std::invalid_argument);
}

TEST(TestCore, TableColumnIterator) {
    using namespace hgps::core;

    const auto dbl_col = DoubleDataTableColumn("double", {1.5, 3.5, 2.0, 0.0, 3.0, 0.0, 5.0},
                                               {true, true, true, false, true, false, true});

    double loop_sum = 0.0;
    for (const auto &item : dbl_col) {
        loop_sum += item.value_or(0.0);
    }

    const auto sum =
        std::accumulate(dbl_col.begin(), dbl_col.end(), 0.0,
                        [](auto a, auto b) { return b.has_value() ? a + b.value() : a; });

    ASSERT_EQ(7U, dbl_col.size());
    ASSERT_EQ(2U, dbl_col.null_count());
    ASSERT_EQ(15.0, sum);
    ASSERT_EQ(loop_sum, sum);
}

TEST(TestCore, CreateDataTable) {
    using namespace hgps::core;

    auto str_values = std::vector<std::string>{"Cat", "Dog", "", "Cow", "Fox"};
    auto flt_values = std::vector<float>{5.7F, 7.13F, 15.37F, 0.0F, 20.75F};
    auto int_values = std::vector<int>{15, 78, 154, 0, 200};

    auto str_builder = StringDataTableColumnBuilder{"String"};
    auto ftl_builder = FloatDataTableColumnBuilder{"Floats"};
    auto dbl_builder = DoubleDataTableColumnBuilder{"Doubles"};
    auto int_builder = IntegerDataTableColumnBuilder{"Integer"};

    for (std::size_t i = 0; i < flt_values.size(); i++) {
        str_values[i].empty() ? str_builder.append_null() : str_builder.append(str_values[i]);
        flt_values[i] == float{} ? ftl_builder.append_null() : ftl_builder.append(flt_values[i]);
        // The baseline wrote `flt_values[i] == double{}` and `flt_values[i] + 1.0`, promoting a
        // float to double implicitly; spelled out here so -Wdouble-promotion stays clean.
        const auto as_double = static_cast<double>(flt_values[i]);
        as_double == 0.0 ? dbl_builder.append_null() : dbl_builder.append(as_double + 1.0);
        int_values[i] == int{} ? int_builder.append_null() : int_builder.append(int_values[i]);
    }

    auto table = DataTable();
    table.add(str_builder.build());
    table.add(ftl_builder.build());
    table.add(dbl_builder.build());
    table.add(int_builder.build());

    const auto &col = table.column("Integer");
    const auto &int_col = dynamic_cast<const IntegerDataTableColumn &>(col);

    const auto slow_value = std::any_cast<int>(col.value(1));
    const auto safe_value = int_col.value_safe(1);
    const auto fast_value = int_col.value_unsafe(1);

    ASSERT_EQ(4U, table.num_columns());
    ASSERT_EQ(5U, table.num_rows());
    ASSERT_EQ(table.num_rows(), int_col.size());
    ASSERT_EQ(78, slow_value);
    ASSERT_EQ(slow_value, *safe_value);
    ASSERT_EQ(slow_value, fast_value);
}

TEST(TestCore, DataTableFailWithColumnLenMismath) {
    using namespace hgps::core;

    auto flt_values = std::vector<float>{7.13F, 15.37F, 0.0F, 20.75F};
    auto int_values = std::vector<int>{154, 0, 200};

    auto table = DataTable();
    table.add(std::make_unique<FloatDataTableColumn>("float", std::move(flt_values),
                                                     std::vector<bool>{true, true, false, true}));

    ASSERT_THROW(table.add(std::make_unique<IntegerDataTableColumn>(
                     "integer", std::move(int_values), std::vector<bool>{true, false, true})),
                 std::invalid_argument);
}

TEST(TestCore, DataTableFailDuplicateColumn) {
    using namespace hgps::core;

    auto flt_values = std::vector<float>{7.13F, 15.37F, 0.0F, 20.75F};
    auto int_values = std::vector<int>{100, 154, 0, 200};

    auto table = DataTable();
    table.add(std::make_unique<FloatDataTableColumn>("Number", std::move(flt_values),
                                                     std::vector<bool>{true, true, false, true}));

    ASSERT_THROW(table.add(std::make_unique<IntegerDataTableColumn>(
                     "number", std::move(int_values), std::vector<bool>{true, true, false, true})),
                 std::invalid_argument);
}

TEST(TestCore, CaseInsensitiveString) {
    using namespace hgps::core;

    const auto *source = "The quick brown fox jumps over the lazy dog";

    ASSERT_TRUE(case_insensitive::equals("fox", "Fox"));
    ASSERT_TRUE(case_insensitive::contains(source, "FOX"));
    ASSERT_TRUE(case_insensitive::starts_with(source, "the"));
    ASSERT_TRUE(case_insensitive::ends_with(source, "Dog"));

    ASSERT_EQ(std::weak_ordering::less, case_insensitive::compare("Dog", "Fox"));
    ASSERT_EQ(std::weak_ordering::equivalent, case_insensitive::compare("fox", "Fox"));
    ASSERT_EQ(std::weak_ordering::greater, case_insensitive::compare("Dog", "Cat"));
}

TEST(TestCore, CaseInsensitiveVectorIndexOf) {
    using namespace hgps::core;

    const std::vector<std::string> source{"The",  "quick", "brown", "fox", "jumps",
                                          "over", "a",     "lazy",  "dog"};

    for (std::size_t i = 0; i < source.size(); i++) {
        ASSERT_EQ(static_cast<int>(i), case_insensitive::index_of(source, source[i]));
        ASSERT_EQ(static_cast<int>(i), case_insensitive::index_of(source, to_upper(source[i])));
    }

    EXPECT_EQ(0, case_insensitive::index_of(source, "the"));
    EXPECT_EQ(2, case_insensitive::index_of(source, "brown"));
    EXPECT_EQ(2, case_insensitive::index_of(source, "BROWN"));
    EXPECT_EQ(8, case_insensitive::index_of(source, "dog"));
    EXPECT_EQ(8, case_insensitive::index_of(source, "DoG"));
    EXPECT_EQ(-1, case_insensitive::index_of(source, "Cat"));
}

TEST(TestCore, CaseInsensitiveVectorContains) {
    using namespace hgps::core;

    const std::vector<std::string> source{"The",  "quick", "brown", "fox", "jumps",
                                          "over", "a",     "lazy",  "dog"};

    for (const auto &word : source) {
        ASSERT_TRUE(case_insensitive::contains(source, word));
        ASSERT_TRUE(case_insensitive::contains(source, to_upper(word)));
    }

    EXPECT_TRUE(case_insensitive::contains(source, "the"));
    EXPECT_TRUE(case_insensitive::contains(source, "DoG"));
    EXPECT_FALSE(case_insensitive::contains(source, "Cat"));
}

TEST(TestCore, CaseInsensitiveMap) {
    using namespace hgps::core;

    const std::map<std::string, int, case_insensitive::comparator> data{
        {"cat", 1},
        {"Dog", 2},
        {"ODD", 3},
    };

    EXPECT_TRUE(data.contains("CAT"));
    EXPECT_TRUE(data.contains("dog"));
    EXPECT_TRUE(data.contains("odd"));
    EXPECT_FALSE(data.contains("Rat"));
}

TEST(TestCore, SplitDelimitedString) {
    using namespace hgps::core;

    const auto parts = split_string("The quick brown fox jumps over the lazy dog", " ");
    const auto csv_parts = split_string("The,quick,brown,fox,jumps,over,the,lazy,dog", ",");

    ASSERT_EQ(9U, parts.size());
    ASSERT_EQ("The", parts.front());
    ASSERT_EQ("dog", parts.back());

    ASSERT_EQ(parts.size(), csv_parts.size());
    ASSERT_EQ(parts.front(), csv_parts.front());
    ASSERT_EQ(parts.back(), csv_parts.back());
}

TEST(TestCore, SplitDelimitedStringEmpty) {
    using namespace hgps::core;
    ASSERT_TRUE(split_string("", ",").empty());
}

TEST(TestCore, SplitDelimitedStringNoDelimiter) {
    using namespace hgps::core;
    const auto parts = split_string("single", ",");
    ASSERT_EQ(1U, parts.size());
    ASSERT_EQ("single", parts[0]);
}

TEST(TestCore, SplitDelimitedStringTrailingDelimiter) {
    using namespace hgps::core;
    // split_string emits non-empty segments only, so a trailing delimiter adds no empty field.
    const auto parts = split_string("a,b,", ",");
    ASSERT_EQ(2U, parts.size());
    ASSERT_EQ("a", parts[0]);
    ASSERT_EQ("b", parts[1]);
}

TEST(TestCore, DataTableColumnLookupIsCaseInsensitiveAndOrderIsPreserved) {
    using namespace hgps::core;

    auto table = DataTable();
    table.add(std::make_unique<IntegerDataTableColumn>("Age", std::vector<int>{1, 2}));
    table.add(std::make_unique<DoubleDataTableColumn>("BMI", std::vector<double>{3.0, 4.0}));

    EXPECT_TRUE(table.contains("age"));
    EXPECT_TRUE(table.contains("bmi"));
    EXPECT_FALSE(table.contains("sodium"));
    EXPECT_FALSE(table.column_if_exists("sodium").has_value());
    EXPECT_TRUE(table.column_if_exists("AGE").has_value());

    // Names keep insertion order: anything that iterates the table iterates a defined sequence.
    ASSERT_EQ(2U, table.names().size());
    EXPECT_EQ("Age", table.names()[0]);
    EXPECT_EQ("BMI", table.names()[1]);
    EXPECT_THROW(table.column("missing"), std::out_of_range);
    EXPECT_THROW(table.column(7), std::out_of_range);
}
