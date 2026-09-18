// The population impact fraction table: its validation, its lookup, and the one place upstream's own
// test pins behaviour this build refuses.
//
// These are the ports of the baseline's `PIFDataItem` (2 tests), `PIFTable` (3), `PIFData` (3),
// `PIFIntegration` (2), `DataManagerPIF` (3), `RepositoryPIF` (2) and `DiseaseModelPIF` (3) suites —
// 18 tests, which the previous run recorded as not ported because PIF was out of scope. Most of them
// assert that a container holds what was put into it; what they do not do is read a real table or
// notice a table with holes in it, which is where this file spends its effort instead
// (docs/test-port-map.md, ADR 0038).
#include "model/disease/pif_table.h"

#include "diagnostics/issue_report.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using hgps::core::Gender;
using hgps::core::PifDataRow;
using hgps::diag::IssueReport;
using hgps::model::PifTable;

/// A complete table over the given ranges, with a value that identifies its own cell so a lookup
/// returning the wrong cell is visible rather than plausible.
std::vector<PifDataRow> complete(int min_age, int max_age, int years) {
    std::vector<PifDataRow> rows;
    for (int year = 0; year < years; ++year) {
        for (const auto gender : {Gender::male, Gender::female}) {
            for (int age = min_age; age <= max_age; ++age) {
                const auto sex = gender == Gender::male ? 0.0 : 0.5;
                rows.push_back(PifDataRow{.gender = gender,
                                          .age = age,
                                          .years_since_intervention = year,
                                          .value = (static_cast<double>(age) / 1000.0) + sex / 100.0 +
                                                   static_cast<double>(year) / 100000.0});
            }
        }
    }
    return rows;
}

} // namespace

TEST(PifTable, AnEmptyTableIsRefused) {
    IssueReport report;
    EXPECT_FALSE(PifTable::build({}, "IF900.csv", report).has_value());
    EXPECT_TRUE(report.has_errors());
}

TEST(PifTable, ReturnsTheValueForEachCell) {
    // The baseline's `PIFTable.GetPIFValues` and `PIFIntegration.PIFDataWorkflow`, for the half of
    // them that is about a complete table.
    IssueReport report;
    const auto table = PifTable::build(complete(0, 10, 3), "IF900.csv", report);
    ASSERT_TRUE(table.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors());

    EXPECT_NEAR(0.005, table->at(5, Gender::male, 0), 1e-12);
    EXPECT_NEAR(0.010, table->at(10, Gender::male, 0), 1e-12);
    EXPECT_NEAR(0.005 + 0.005, table->at(5, Gender::female, 0), 1e-12);
    EXPECT_NEAR(0.005 + 0.00002, table->at(5, Gender::male, 2), 1e-12);
}

TEST(PifTable, OutsideTheTabulatedRangeIsZeroRatherThanAnError) {
    // A person older than the table's last age, or a year beyond its horizon, is an ordinary thing a
    // run meets. "This policy does nothing here" is the right answer for an untabulated cell; a hole
    // *inside* the range is the error case, and it is the next test.
    IssueReport report;
    const auto table = PifTable::build(complete(0, 10, 3), "IF900.csv", report);
    ASSERT_TRUE(table.has_value()) << report.to_string();

    EXPECT_DOUBLE_EQ(0.0, table->at(11, Gender::male, 0));
    EXPECT_DOUBLE_EQ(0.0, table->at(5, Gender::male, 3));
    EXPECT_DOUBLE_EQ(0.0, table->at(200, Gender::female, 99));

    // A sex the table cannot describe gets nothing rather than somebody else's row.
    EXPECT_DOUBLE_EQ(0.0, table->at(5, Gender::unknown, 0));
}

TEST(PifTable, AHoleInsideTheRangeIsAnErrorRatherThanAZero) {
    // This is where this build parts company with the baseline's own test. `PIFTable.GetPIFValues`
    // adds three items — ages 25 and 30, years 3, 5 and 10 — and then *asserts* that the cells nobody
    // mentioned read as exactly 0.0. Upstream's `build_hash_table` sizes a dense array from the
    // observed minima and maxima and leaves everything else default-constructed, so a file missing
    // rows produces a policy that works for part of the population and says nothing.
    //
    // Zero is a meaningful value in these tables — most cells of the real ones are zero — so a hole
    // is indistinguishable from a tabulated zero. Here it is an error naming the first missing
    // combination (deviation B-26).
    std::vector<PifDataRow> sparse{
        PifDataRow{.gender = Gender::male, .age = 25, .years_since_intervention = 5, .value = 0.3},
        PifDataRow{.gender = Gender::female, .age = 30, .years_since_intervention = 3, .value = 0.2},
        PifDataRow{.gender = Gender::male, .age = 25, .years_since_intervention = 10, .value = 0.4},
    };

    IssueReport report;
    EXPECT_FALSE(PifTable::build(sparse, "IF900.csv", report).has_value());
    ASSERT_TRUE(report.has_errors()) << report.to_string();

    const auto text = report.to_string();
    EXPECT_NE(std::string::npos, text.find("no population impact fraction for"));
    // It says what the file does cover, so the reader can see how big the hole is.
    EXPECT_NE(std::string::npos, text.find("ages 25"));
    EXPECT_NE(std::string::npos, text.find("years 3"));
}

TEST(PifTable, ADuplicateCellIsAnErrorNamingBoth) {
    auto rows = complete(0, 2, 1);
    rows.push_back(rows.front());
    rows.back().value = 0.9;

    IssueReport report;
    EXPECT_FALSE(PifTable::build(rows, "IF900.csv", report).has_value());
    EXPECT_NE(std::string::npos, report.to_string().find("a second population impact fraction"));
}

TEST(PifTable, AValueOutsideTheUnitIntervalIsAnErrorRatherThanAClamp) {
    // Upstream clamps and warns. A fraction above one makes an incidence probability negative and one
    // below zero makes a policy cause disease; neither is a number this program can act on, and the
    // real data is in [0, 0.159], so refusing costs nothing that exists (deviation B-26).
    for (const double bad : {1.5, -0.1}) {
        auto rows = complete(0, 2, 1);
        rows.front().value = bad;

        IssueReport report;
        EXPECT_FALSE(PifTable::build(rows, "IF900.csv", report).has_value()) << bad;
        const auto text = report.to_string();
        EXPECT_NE(std::string::npos, text.find("outside [0, 1]")) << bad;
        EXPECT_NE(std::string::npos, text.find("Upstream clamps")) << bad;
    }
}

TEST(PifTable, ANegativeAgeOrYearIsRefused) {
    for (const bool age_is_bad : {true, false}) {
        auto rows = complete(0, 2, 1);
        if (age_is_bad) {
            rows.front().age = -1;
        } else {
            rows.front().years_since_intervention = -1;
        }

        IssueReport report;
        EXPECT_FALSE(PifTable::build(rows, "IF900.csv", report).has_value());
        EXPECT_TRUE(report.has_errors());
    }
}

TEST(PifTable, ReportsItsOwnRangeAndLargestValue) {
    IssueReport report;
    const auto table = PifTable::build(complete(3, 9, 4), "IF900.csv", report);
    ASSERT_TRUE(table.has_value()) << report.to_string();

    EXPECT_EQ(3, table->min_age());
    EXPECT_EQ(9, table->max_age());
    EXPECT_EQ(0, table->min_year());
    EXPECT_EQ(3, table->max_year());
    EXPECT_FALSE(table->empty());
    EXPECT_GT(table->largest(), 0.0) << "the largest value is what the load-time 'all zero' note uses";
}

TEST(PifTable, ATableOfZerosIsValidAndSaysSoThroughLargest) {
    // A policy with no effect on a disease is a legitimate scenario — the real pack has cells like it
    // everywhere — so it loads. `largest()` is how the caller notices and warns.
    auto rows = complete(0, 4, 2);
    for (auto &row : rows) {
        row.value = 0.0;
    }

    IssueReport report;
    const auto table = PifTable::build(rows, "IF900.csv", report);
    ASSERT_TRUE(table.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors());
    EXPECT_DOUBLE_EQ(0.0, table->largest());
    EXPECT_DOUBLE_EQ(0.0, table->at(2, Gender::male, 1));
}

TEST(PifTable, ADefaultTableIsEmptyAndAlwaysZero) {
    // The baseline's `PIFTable.EmptyTable` and `PIFData.EmptyData`: a disease with no fractions must
    // not change any probability.
    const PifTable table;
    EXPECT_TRUE(table.empty());
    EXPECT_EQ(0U, table.size());
    EXPECT_DOUBLE_EQ(0.0, table.at(40, Gender::male, 2));
}
