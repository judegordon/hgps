// New here. The baseline validates config against JSON Schema through jsoncons and reports
// "Invalid configuration - : Required property 'data' not found." with no file, line or column
// (docs/decisions/0022-hand-written-config-validation.md).
#include "io/json.h"

#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <fstream>

namespace {

using hgps::diag::IssueCode;
using hgps::io::JsonCursor;

std::filesystem::path write_json(const std::string &name, const std::string &contents) {
    const auto dir = hgps::test::scratch_dir("json_reader");
    const auto path = dir / name;
    std::ofstream stream{path};
    stream << contents;
    return path;
}

JsonCursor cursor_over(const nlohmann::json &document, hgps::diag::IssueReport &report) {
    return JsonCursor{document, "test.json", "", report};
}

} // namespace

TEST(TestIo_Json, ReadsAValidDocument) {
    hgps::diag::IssueReport report;
    const auto path = write_json("ok.json", R"({"version": 2, "name": "France"})");

    const auto document = hgps::io::read_json(path, report);
    ASSERT_TRUE(document.has_value());
    EXPECT_FALSE(report.has_errors());
    EXPECT_EQ(2, (*document)["version"].get<int>());
}

TEST(TestIo_Json, AParseErrorCarriesALineAndColumn) {
    hgps::diag::IssueReport report;
    const auto path = write_json("bad.json", "{\n  \"version\": 2,\n  \"oops\"\n}\n");

    EXPECT_FALSE(hgps::io::read_json(path, report).has_value());
    ASSERT_EQ(1U, report.issues().size());

    const auto &issue = report.issues().front();
    EXPECT_EQ(IssueCode::json_parse_error, issue.code);
    EXPECT_EQ(path.string(), issue.location.file);
    ASSERT_TRUE(issue.location.line.has_value());
    EXPECT_EQ(4U, *issue.location.line);
    ASSERT_TRUE(issue.location.column.has_value());
}

TEST(TestIo_Json, AMissingFileIsALocatedIssue) {
    hgps::diag::IssueReport report;
    EXPECT_FALSE(hgps::io::read_json("/no/such/config.json", report).has_value());
    EXPECT_TRUE(report.contains(IssueCode::file_not_found));
}

TEST(TestIo_Json, MissingAndWrongTypedFieldsAreReportedWithAJsonPointer) {
    hgps::diag::IssueReport report;
    const auto document = nlohmann::json::parse(R"({"running": {"seed": "abc"}})");
    const auto root = cursor_over(document, report);

    const auto running = root.object("running");
    ASSERT_TRUE(running.has_value());

    EXPECT_FALSE(running->integer("seed").has_value());
    EXPECT_FALSE(running->integer("start_time").has_value());

    ASSERT_EQ(2U, report.issues().size());
    EXPECT_EQ(IssueCode::config_wrong_type, report.issues()[0].code);
    EXPECT_EQ("/running/seed", report.issues()[0].location.field);
    EXPECT_EQ(IssueCode::config_missing_required, report.issues()[1].code);
    EXPECT_EQ("/running/start_time", report.issues()[1].location.field);
}

TEST(TestIo_Json, ReadsEveryScalarKind) {
    hgps::diag::IssueReport report;
    const auto document = nlohmann::json::parse(R"({
        "text": "hello", "count": 7, "ratio": 0.25, "flag": true,
        "numbers": [1, 2.5, 3], "words": ["a", "b"], "negative": -3
    })");
    const auto root = cursor_over(document, report);

    EXPECT_EQ("hello", root.string("text"));
    EXPECT_EQ(7, root.integer("count"));
    EXPECT_EQ(7U, root.unsigned_integer("count"));
    EXPECT_DOUBLE_EQ(0.25, *root.number("ratio"));
    EXPECT_TRUE(*root.boolean("flag"));
    EXPECT_EQ(std::vector<double>({1.0, 2.5, 3.0}), root.number_array("numbers"));
    EXPECT_EQ(std::vector<std::string>({"a", "b"}), root.string_array("words"));
    EXPECT_FALSE(report.has_errors());

    // A negative value where a count is required is a value error, not a type error.
    EXPECT_FALSE(root.unsigned_integer("negative").has_value());
    EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
}

TEST(TestIo_Json, AnAppliedDefaultIsAWarningThatNamesTheValue) {
    hgps::diag::IssueReport report;
    const auto document = nlohmann::json::parse(R"({})");
    const auto root = cursor_over(document, report);

    EXPECT_EQ(1, root.integer_or_default("trial_runs", 1));
    EXPECT_TRUE(root.boolean_or_default("enabled", true));
    EXPECT_EQ("csv", root.string_or_default("format", "csv"));

    EXPECT_FALSE(report.has_errors());
    EXPECT_EQ(3U, report.warning_count());
    EXPECT_NE(std::string::npos, report.issues().front().message.find("default"));
    EXPECT_NE(std::string::npos, report.issues().front().message.find('1'));
}

TEST(TestIo_Json, UnknownMembersAreRejectedAndNearMissesAreSuggested) {
    hgps::diag::IssueReport report;
    const auto document =
        nlohmann::json::parse(R"({"seed": 1, "start_tim": 2010, "totally_unrelated": 3})");
    const auto root = cursor_over(document, report);

    root.reject_unknown_members({"seed", "start_time", "stop_time"});

    ASSERT_EQ(2U, report.error_count());
    EXPECT_EQ(IssueCode::config_unknown_property, report.issues()[0].code);
    EXPECT_NE(std::string::npos, report.issues()[0].message.find("start_time"))
        << report.issues()[0].message;
    EXPECT_EQ(std::string::npos, report.issues()[1].message.find("did you mean"));
}

TEST(TestIo_Json, ARemovedMemberIsRejectedWithItsReplacement) {
    hgps::diag::IssueReport report;
    const auto document = nlohmann::json::parse(R"({"sync_timeout_ms": 15000})");
    const auto root = cursor_over(document, report);

    root.reject_removed_member("sync_timeout_ms",
                               "scenarios now run sequentially, so there is nothing to time out");
    root.reject_removed_member("not_present", "irrelevant");

    ASSERT_EQ(1U, report.error_count());
    EXPECT_EQ(IssueCode::config_removed_property, report.issues().front().code);
    EXPECT_NE(std::string::npos, report.issues().front().message.find("sequentially"));
}

TEST(TestIo_Json, ArrayCursorsCarryTheirIndexInThePointer) {
    hgps::diag::IssueReport report;
    const auto document =
        nlohmann::json::parse(R"({"risk_factors": [{"name": "BMI"}, {"level": 1}]})");
    const auto root = cursor_over(document, report);

    const auto items = root.array("risk_factors");
    ASSERT_EQ(2U, items.size());
    EXPECT_EQ("/risk_factors/0", items[0].pointer());

    EXPECT_EQ("BMI", items[0].string("name"));
    EXPECT_FALSE(items[1].string("name").has_value());
    ASSERT_EQ(1U, report.error_count());
    EXPECT_EQ("/risk_factors/1/name", report.issues().front().location.field);
}

TEST(TestIo_Json, AnOptionalObjectIsSilentWhenAbsentAndTypedWhenPresent) {
    hgps::diag::IssueReport report;
    const auto document = nlohmann::json::parse(R"({"output": 7})");
    const auto root = cursor_over(document, report);

    EXPECT_FALSE(root.optional_object("individual_id_tracking").has_value());
    EXPECT_FALSE(report.has_errors());

    EXPECT_FALSE(root.optional_object("output").has_value());
    EXPECT_TRUE(report.contains(IssueCode::config_wrong_type));
}
