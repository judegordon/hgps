// New here in this shape. The baseline's CSV path is rapidcsv plus a loader that prints coloured
// messages and throws std::runtime_error("Error parsing dataset."); there is no unit test of it at
// all. Every case below is a located diagnostic that replaces one of those messages.
#include "io/csv_reader.h"

#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace {

std::filesystem::path write_csv(const std::string &name, const std::string &contents) {
    const auto dir = hgps::test::scratch_dir("csv_reader");
    const auto path = dir / name;
    std::ofstream stream{path, std::ios::binary};
    stream << contents;
    return path;
}

} // namespace

TEST(TestIo_CsvReader, ReadsHeaderAndRows) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("simple.csv", "Age,Gender,BMI\n30,1,24.5\n41,2,31.25\n");

    const auto document = hgps::io::read_csv(path, {}, report);
    ASSERT_TRUE(document.has_value());
    EXPECT_FALSE(report.has_errors());

    EXPECT_EQ(3U, document->num_columns());
    EXPECT_EQ(2U, document->num_rows());
    EXPECT_EQ("Age", document->headers().front());

    ASSERT_TRUE(document->column_index("bmi").has_value());
    EXPECT_EQ(2U, *document->column_index("bmi"));
    EXPECT_FALSE(document->column_index("weight").has_value());

    EXPECT_EQ("24.5", document->field(0, 2));
    EXPECT_EQ(31.25, document->field_as_double(1, 2, report));
    EXPECT_EQ(41, document->field_as_int(1, 0, report));

    // The header is line 1, so the first data row is line 2 — which is what a diagnostic needs.
    EXPECT_EQ(2U, document->line_of(0));
    EXPECT_EQ(3U, document->line_of(1));
}

TEST(TestIo_CsvReader, MissingFileIsALocatedIssue) {
    hgps::diag::IssueReport report;
    const auto document = hgps::io::read_csv("/no/such/file.csv", {}, report);

    EXPECT_FALSE(document.has_value());
    ASSERT_EQ(1U, report.issues().size());
    EXPECT_EQ(hgps::diag::IssueCode::file_not_found, report.issues().front().code);
    EXPECT_EQ("/no/such/file.csv", report.issues().front().location.file);
}

TEST(TestIo_CsvReader, EmptyFileIsRejected) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("empty.csv", "");

    EXPECT_FALSE(hgps::io::read_csv(path, {}, report).has_value());
    EXPECT_TRUE(report.contains(hgps::diag::IssueCode::csv_empty));
}

TEST(TestIo_CsvReader, RaggedRowIsReportedWithItsLineNumber) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("ragged.csv", "Age,BMI\n30,24.5\n41\n52,28.0\n");

    const auto document = hgps::io::read_csv(path, {}, report);
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(report.contains(hgps::diag::IssueCode::csv_ragged_row));

    const auto &issue = report.issues().front();
    ASSERT_TRUE(issue.location.line.has_value());
    EXPECT_EQ(3U, *issue.location.line);

    // Reading continues, so the row after the bad one is still reported on.
    EXPECT_EQ("52", document->field(2, 0));
}

TEST(TestIo_CsvReader, RaggedRowsCanBeAllowed) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("ragged_ok.csv", "Age,BMI\n30\n");

    hgps::io::CsvOptions options;
    options.allow_ragged_rows = true;
    const auto document = hgps::io::read_csv(path, options, report);

    ASSERT_TRUE(document.has_value());
    EXPECT_FALSE(report.has_errors());
    EXPECT_EQ("", document->field(0, 1));
}

TEST(TestIo_CsvReader, DuplicateColumnIsReported) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("dup.csv", "Age,BMI,age\n1,2,3\n");

    hgps::io::read_csv(path, {}, report);
    EXPECT_TRUE(report.contains(hgps::diag::IssueCode::csv_duplicate_column));
}

TEST(TestIo_CsvReader, HandlesQuotedFieldsAndTrimsUnquotedOnes) {
    hgps::diag::IssueReport report;
    const auto path =
        write_csv("quoted.csv", "Name,Note\n\"Doe, John\",\"said \"\"hi\"\"\"\n  spaced  , x \n");

    const auto document = hgps::io::read_csv(path, {}, report);
    ASSERT_TRUE(document.has_value());
    EXPECT_FALSE(report.has_errors());

    EXPECT_EQ("Doe, John", document->field(0, 0));
    EXPECT_EQ("said \"hi\"", document->field(0, 1));
    EXPECT_EQ("spaced", document->field(1, 0));
    EXPECT_EQ("x", document->field(1, 1));
}

TEST(TestIo_CsvReader, HandlesCrlfAndAByteOrderMark) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("crlf.csv", "\xEF\xBB\xBF" "Age,BMI\r\n30,24.5\r\n");

    const auto document = hgps::io::read_csv(path, {}, report);
    ASSERT_TRUE(document.has_value());
    EXPECT_FALSE(report.has_errors());
    EXPECT_EQ("Age", document->headers().front());
    EXPECT_EQ("24.5", document->field(0, 1));
}

TEST(TestIo_CsvReader, ASemicolonDelimitedFileIsReadWithTheConfiguredDelimiter) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("semi.csv", "Age;BMI\n30;24.5\n");

    hgps::io::CsvOptions options;
    options.delimiter = ';';
    const auto document = hgps::io::read_csv(path, options, report);

    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(2U, document->num_columns());
    EXPECT_EQ("24.5", document->field(0, 1));
}

TEST(TestIo_CsvReader, BadNumberNamesTheFileLineColumnAndValue) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("bad.csv", "Age,BMI\n30,not-a-number\n");

    const auto document = hgps::io::read_csv(path, {}, report);
    ASSERT_TRUE(document.has_value());

    EXPECT_FALSE(document->field_as_double(0, 1, report).has_value());
    ASSERT_EQ(1U, report.issues().size());

    const auto &issue = report.issues().front();
    EXPECT_EQ(hgps::diag::IssueCode::csv_bad_value, issue.code);
    EXPECT_EQ("BMI", issue.location.field);
    ASSERT_TRUE(issue.location.line.has_value());
    EXPECT_EQ(2U, *issue.location.line);
    ASSERT_TRUE(issue.location.column.has_value());
    EXPECT_EQ(2U, *issue.location.column);
    EXPECT_NE(std::string::npos, issue.message.find("not-a-number"));
}

TEST(TestIo_CsvReader, PartiallyNumericFieldIsRejected) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("partial.csv", "Age\n30kg\n");

    const auto document = hgps::io::read_csv(path, {}, report);
    ASSERT_TRUE(document.has_value());

    // std::stod("30kg") returns 30 and stops; accepting that would silently change a number.
    EXPECT_FALSE(document->field_as_double(0, 0, report).has_value());
    EXPECT_TRUE(report.contains(hgps::diag::IssueCode::csv_bad_value));
}

TEST(TestIo_CsvReader, EmptyFieldIsNullNotZero) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("nulls.csv", "Age,BMI\n30,\n");

    const auto document = hgps::io::read_csv(path, {}, report);
    ASSERT_TRUE(document.has_value());

    EXPECT_FALSE(document->field_as_double(0, 1, report).has_value());
    EXPECT_FALSE(report.has_errors());
}

TEST(TestIo_CsvReader, LoadsADataTableInTheDeclaredColumnOrder) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("table.csv", "BMI,Age,Name\n24.5,30,alice\n31.0,41,bob\n");

    const std::vector<hgps::io::CsvColumnSpec> columns{
        {"Age", "integer"}, {"BMI", "double"}, {"Name", "string"}};

    const auto table = hgps::io::load_datatable_from_csv(path, columns, {}, report);
    ASSERT_TRUE(table.has_value());
    EXPECT_FALSE(report.has_errors());

    ASSERT_EQ(3U, table->num_columns());
    EXPECT_EQ(2U, table->num_rows());

    // Declared order, not the file's order and not alphabetical.
    EXPECT_EQ("Age", table->names()[0]);
    EXPECT_EQ("BMI", table->names()[1]);
    EXPECT_EQ("Name", table->names()[2]);

    const auto &age = dynamic_cast<const hgps::core::IntegerDataTableColumn &>(table->column("Age"));
    EXPECT_EQ(30, age.value_unsafe(0));
    const auto &bmi =
        dynamic_cast<const hgps::core::DoubleDataTableColumn &>(table->column("BMI"));
    EXPECT_DOUBLE_EQ(31.0, bmi.value_unsafe(1));
    const auto &name =
        dynamic_cast<const hgps::core::StringDataTableColumn &>(table->column("Name"));
    EXPECT_EQ("alice", name.value_unsafe(0));
}

TEST(TestIo_CsvReader, MissingDeclaredColumnIsReportedByName) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("short.csv", "Age\n30\n");

    const std::vector<hgps::io::CsvColumnSpec> columns{{"Age", "integer"}, {"BMI", "double"}};
    EXPECT_FALSE(hgps::io::load_datatable_from_csv(path, columns, {}, report).has_value());

    ASSERT_TRUE(report.contains(hgps::diag::IssueCode::csv_missing_column));
    EXPECT_EQ("BMI", report.issues().front().location.field);
}

TEST(TestIo_CsvReader, EveryBadCellIsReportedNotJustTheFirst) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("many_bad.csv", "Age,BMI\nx,1.0\n2,y\nz,w\n");

    const std::vector<hgps::io::CsvColumnSpec> columns{{"Age", "integer"}, {"BMI", "double"}};
    EXPECT_FALSE(hgps::io::load_datatable_from_csv(path, columns, {}, report).has_value());

    // Four bad cells: this is the behaviour the two-tier diagnostics design exists for.
    EXPECT_EQ(4U, report.error_count());
}

TEST(TestIo_CsvReader, UnknownDeclaredTypeIsRejected) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("type.csv", "Age\n30\n");

    const std::vector<hgps::io::CsvColumnSpec> columns{{"Age", "int64"}};
    EXPECT_FALSE(hgps::io::load_datatable_from_csv(path, columns, {}, report).has_value());
    EXPECT_TRUE(report.contains(hgps::diag::IssueCode::config_bad_value));
}

TEST(TestIo_CsvReader, LoadsBaselineAdjustmentsKeyedByFactor) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("factors.csv", "age,BMI,Energy\n0,20.1,2000\n1,20.4,2100\n");

    const auto adjustments = hgps::io::load_baseline_adjustments_from_csv(path, {}, report);
    ASSERT_TRUE(adjustments.has_value());
    EXPECT_FALSE(report.has_errors());

    ASSERT_EQ(2U, adjustments->size());
    ASSERT_TRUE(adjustments->contains(hgps::core::Identifier{"bmi"}));
    EXPECT_DOUBLE_EQ(20.4, adjustments->at(hgps::core::Identifier{"bmi"})[1]);
    EXPECT_DOUBLE_EQ(2000.0, adjustments->at(hgps::core::Identifier{"energy"})[0]);
}

TEST(TestIo_CsvReader, BaselineAdjustmentsMustStartWithAge) {
    hgps::diag::IssueReport report;
    const auto path = write_csv("factors_bad.csv", "year,BMI\n0,20.1\n");

    EXPECT_FALSE(hgps::io::load_baseline_adjustments_from_csv(path, {}, report).has_value());
    EXPECT_TRUE(report.contains(hgps::diag::IssueCode::csv_missing_column));
}
