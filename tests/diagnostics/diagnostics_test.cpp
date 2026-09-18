// New here. The baseline has one exception type for both programmer errors and user mistakes, so
// there is nothing of this shape to port (docs/decisions/0007-two-tier-diagnostics.md).
#include "diagnostics/internal_error.h"
#include "diagnostics/issue.h"
#include "diagnostics/issue_report.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>

using namespace hgps::diag;

TEST(TestDiagnostics_InternalError, CarriesItsThrowSiteLocation) {
    const auto line_before = static_cast<std::uint_least32_t>(__LINE__);
    try {
        throw InternalError("something impossible happened");
    } catch (const InternalError &error) {
        EXPECT_STREQ("something impossible happened", error.what());
        EXPECT_EQ(line_before + 2, error.line());
        EXPECT_NE(nullptr, error.file_name());
        EXPECT_NE(std::string::npos, std::string{error.file_name()}.find("diagnostics_test.cpp"));
        EXPECT_NE(std::string::npos, error.describe().find("something impossible happened"));
        EXPECT_NE(std::string::npos, error.describe().find("diagnostics_test.cpp"));
    }
}

TEST(TestDiagnostics_InternalError, IsCatchableAsAStandardException) {
    // main's last-resort handler catches std::exception, so this has to hold.
    try {
        throw InternalError("boom");
    } catch (const std::exception &error) {
        EXPECT_STREQ("boom", error.what());
    }
}

TEST(TestDiagnostics_Issue, EveryCodeHasADistinctName) {
    // A closed enum is only useful if each member is reportable, and an added member without a
    // name would otherwise silently print "unknown".
    const auto codes = {IssueCode::file_not_found,
                        IssueCode::file_unreadable,
                        IssueCode::json_parse_error,
                        IssueCode::config_missing_required,
                        IssueCode::config_unknown_property,
                        IssueCode::config_wrong_type,
                        IssueCode::config_bad_value,
                        IssueCode::config_removed_property,
                        IssueCode::config_undefined_variable,
                        IssueCode::config_schema_mismatch,
                        IssueCode::config_default_applied,
                        IssueCode::model_file_not_found,
                        IssueCode::model_unknown_name,
                        IssueCode::model_unknown_predictor,
                        IssueCode::model_missing_key,
                        IssueCode::model_bad_value,
                        IssueCode::model_dimension_mismatch,
                        IssueCode::model_default_applied,
                        IssueCode::csv_empty,
                        IssueCode::csv_missing_column,
                        IssueCode::csv_duplicate_column,
                        IssueCode::csv_ragged_row,
                        IssueCode::csv_bad_value,
                        IssueCode::data_index_invalid,
                        IssueCode::data_missing_file,
                        IssueCode::data_disease_not_in_tree,
                        IssueCode::data_disease_not_in_registry,
                        IssueCode::data_country_unknown,
                        IssueCode::data_checksum_missing,
                        IssueCode::data_checksum_mismatch,
                        IssueCode::data_source_invalid,
                        IssueCode::data_tool_missing,
                        IssueCode::data_download_failed,
                        IssueCode::data_extract_failed,
                        IssueCode::feature_not_implemented};

    std::set<std::string> names;
    for (const auto code : codes) {
        const auto name = std::string{to_string(code)};
        EXPECT_NE("unknown", name);
        names.insert(name);
    }

    EXPECT_EQ(codes.size(), names.size()) << "two issue codes share a name";
}

TEST(TestDiagnostics_Issue, LocationRendersAsMuchAsItKnows) {
    EXPECT_EQ("", IssueLocation{}.to_string());
    EXPECT_EQ("france.json", (IssueLocation{.file = "france.json"}).to_string());
    EXPECT_EQ("france.json:34", (IssueLocation{.file = "france.json", .line = 34U}).to_string());
    EXPECT_EQ("france.json:34:5",
              (IssueLocation{.file = "france.json", .line = 34U, .column = 5U}).to_string());
    EXPECT_EQ("france.json:34:5 (/running/seed)",
              (IssueLocation{.file = "france.json",
                             .field = "/running/seed",
                             .line = 34U,
                             .column = 5U})
                  .to_string());
    EXPECT_EQ("/running/seed", (IssueLocation{.field = "/running/seed"}).to_string());
}

TEST(TestDiagnostics_Report, AccumulatesInsteadOfThrowing) {
    IssueReport report;
    EXPECT_TRUE(report.empty());
    EXPECT_FALSE(report.has_errors());

    report.error(IssueCode::config_missing_required,
                 IssueLocation{.file = "c.json", .field = "/running/seed"},
                 "'seed' is required and must be an integer");
    report.warning(IssueCode::config_default_applied,
                   IssueLocation{.file = "c.json", .field = "/running/trial_runs"},
                   "'trial_runs' not given; using the documented default of 1");
    report.error(IssueCode::config_unknown_property,
                 IssueLocation{.file = "c.json", .field = "/running/sync_timeout_ms"},
                 "unknown property; it was removed in config v2");

    EXPECT_FALSE(report.empty());
    EXPECT_TRUE(report.has_errors());
    EXPECT_EQ(2U, report.error_count());
    EXPECT_EQ(1U, report.warning_count());
    EXPECT_EQ(3U, report.issues().size());

    // Order is preserved: the user reads problems in the order they were found.
    EXPECT_EQ(IssueCode::config_missing_required, report.issues()[0].code);
    EXPECT_EQ(IssueCode::config_default_applied, report.issues()[1].code);
    EXPECT_EQ(IssueCode::config_unknown_property, report.issues()[2].code);

    EXPECT_TRUE(report.contains(IssueCode::config_unknown_property));
    EXPECT_FALSE(report.contains(IssueCode::csv_bad_value));

    const auto text = report.to_string();
    EXPECT_NE(std::string::npos, text.find("config_missing_required"));
    EXPECT_NE(std::string::npos, text.find("/running/seed"));
    EXPECT_NE(std::string::npos, text.find("2 errors, 1 warning"));
}

TEST(TestDiagnostics_Report, MergePreservesOrderAndCounts) {
    IssueReport first;
    first.error(IssueCode::csv_missing_column, IssueLocation{.file = "a.csv"}, "no 'Age' column");

    IssueReport second;
    second.warning(IssueCode::csv_ragged_row, IssueLocation{.file = "b.csv", .line = 9U},
                   "row has 3 fields, header has 4");
    second.error(IssueCode::csv_bad_value,
                 IssueLocation{.file = "b.csv", .field = "BMI", .line = 12U, .column = 3U},
                 "'abc' is not a number");

    first.merge(second);

    EXPECT_EQ(3U, first.issues().size());
    EXPECT_EQ(2U, first.error_count());
    EXPECT_EQ(1U, first.warning_count());
    EXPECT_EQ(IssueCode::csv_missing_column, first.issues()[0].code);
    EXPECT_EQ(IssueCode::csv_bad_value, first.issues()[2].code);
}

TEST(TestDiagnostics_Report, ClearResetsCounts) {
    IssueReport report;
    report.error(IssueCode::file_not_found, IssueLocation{.file = "x"}, "missing");
    report.clear();

    EXPECT_TRUE(report.empty());
    EXPECT_FALSE(report.has_errors());
    EXPECT_EQ(0U, report.error_count());
    EXPECT_EQ(0U, report.warning_count());
}
