// The result writer: the main CSV, the income-stratified CSVs and the metadata JSON.
//
// Ported from the baseline's ResultFileWriter suite, whose five tests are all about the
// income-stratum files, plus the properties this implementation adds: one owner per file, a
// defined row order, no timestamp in the CSV, and the seed actually used in the metadata.
#include "output/result_writer.h"

#include "core/income_category_layout.h"
#include "model/results.h"
#include "sim/engine.h"
#include "support/test_paths.h"

#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::core::Gender;
using hgps::core::Income;
using hgps::model::DataSeries;
using hgps::model::ModelResult;
using hgps::output::ResultWriter;
using hgps::output::RunMetadata;
using hgps::sim::ResultRow;
using hgps::sim::ScenarioType;

std::vector<std::string> read_lines(const std::filesystem::path &path) {
    std::vector<std::string> lines;
    std::ifstream stream{path};
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

/// The nth comma-separated field of a CSV line.
std::string field(const std::string &line, std::size_t index) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < index; ++i) {
        start = line.find(',', start);
        if (start == std::string::npos) {
            return {};
        }
        ++start;
    }
    const auto end = line.find(',', start);
    return line.substr(start, end == std::string::npos ? end : end - start);
}

RunMetadata metadata() {
    return RunMetadata{.model = "test",
                       .version = "1",
                       .intervention = "none",
                       .job_id = 1,
                       .seed = 123456789U,
                       .run_seeds = {987654321U},
                       .config_path = "config.json",
                       .config_sha256 = std::string(64, 'a'),
                       .country = "FRA",
                       .start_time = 2022,
                       .stop_time = 2022,
                       .trial_runs = 1,
                       .cohort_size = 6};
}

/// One year of results over `ages` age bands, with the given income strata populated.
ResultRow row_with_income(std::size_t ages, const std::vector<Income> &strata) {
    ModelResult result{ages};
    result.series.add_channels({"count", "mean_income_category"});

    for (const auto income : strata) {
        for (const auto sex : {Gender::male, Gender::female}) {
            // Touching the channel creates the stratum, which is what the writer looks for.
            result.series.at(sex, income, "count");
            result.series.at(sex, income, "mean_income_category");
        }
    }

    result.population_by_income = hgps::model::ResultByIncome{};

    return ResultRow{.source = ScenarioType::baseline,
                     .source_name = "Baseline",
                     .run = 1U,
                     .time = 2022,
                     .result = std::move(result)};
}

} // namespace

TEST(ResultWriterTest, IncomeCsvIncludesZeroCountAgeGenderRows) {
    // The baseline's IncomeCsvIncludesZeroCountAgeGenderRows: an age band with nobody in it still
    // gets its two rows, because a reader joining on (year, sex, age) must find every band.
    const auto directory = hgps::test::scratch_dir("writer_income_zero_rows");
    auto row = row_with_income(3, {Income::low});

    row.result.series.at(Gender::male, Income::low, "count").at(0) = 1.0;
    row.result.series.at(Gender::female, Income::low, "count").at(0) = 0.0;
    // Band 1 is left at zero for both sexes on purpose.
    row.result.series.at(Gender::male, Income::low, "count").at(2) = 2.0;
    row.result.series.at(Gender::female, Income::low, "count").at(2) = 3.0;
    row.result.population_by_income->at(Income::low) = 1.0;

    {
        ResultWriter writer{directory / "result.json", metadata(), true,
                            hgps::core::income_category_layout_from_config("3")};
        writer.write(row);
    }

    const auto lines = read_lines(directory / "result_LowIncome.csv");
    ASSERT_FALSE(lines.empty());

    // Header plus three ages times two sexes.
    EXPECT_EQ(7U, lines.size());

    std::size_t band_one_rows = 0;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (field(lines[i], 4) == "1") {
            ++band_one_rows;
            EXPECT_EQ("0", field(lines[i], 5)) << "an empty band's count should be written as 0";
        }
    }
    EXPECT_EQ(2U, band_one_rows);
}

TEST(ResultWriterTest, FourIncomeCategoriesCreateAllStratumFiles) {
    const auto directory = hgps::test::scratch_dir("writer_income_four");
    auto row = row_with_income(1, {Income::low, Income::lowermiddle, Income::uppermiddle,
                                   Income::high});
    row.result.series.at(Gender::male, Income::low, "count").at(0) = 1.0;

    {
        ResultWriter writer{directory / "result.json", metadata(), true,
                            hgps::core::income_category_layout_from_config("4")};
        writer.write(row);
    }

    for (const auto *name : {"result_LowIncome.csv", "result_LowerMiddleIncome.csv",
                             "result_UpperMiddleIncome.csv", "result_HighIncome.csv"}) {
        EXPECT_TRUE(std::filesystem::is_regular_file(directory / name)) << name << " is missing";
    }
    EXPECT_FALSE(std::filesystem::exists(directory / "result_MiddleIncome.csv"))
        << "the three-category 'middle' stratum is not part of a four-category layout";
}

TEST(ResultWriterTest, ThreeIncomeCategoriesExcludeFourCategoryMiddleStrata) {
    const auto directory = hgps::test::scratch_dir("writer_income_three");
    auto row = row_with_income(1, {Income::low, Income::middle, Income::high});
    row.result.series.at(Gender::male, Income::middle, "count").at(0) = 1.0;

    {
        ResultWriter writer{directory / "result.json", metadata(), true,
                            hgps::core::income_category_layout_from_config("3")};
        writer.write(row);
    }

    for (const auto *name : {"result_LowIncome.csv", "result_MiddleIncome.csv",
                             "result_HighIncome.csv"}) {
        EXPECT_TRUE(std::filesystem::is_regular_file(directory / name)) << name << " is missing";
    }
    for (const auto *name : {"result_LowerMiddleIncome.csv", "result_UpperMiddleIncome.csv"}) {
        EXPECT_FALSE(std::filesystem::exists(directory / name)) << name << " should not exist";
    }
}

TEST(ResultWriterTest, FiveIncomeCategoriesCreateAllStratumFiles) {
    const auto directory = hgps::test::scratch_dir("writer_income_five");
    auto row = row_with_income(1, {Income::low, Income::lowermiddle, Income::middle,
                                   Income::uppermiddle, Income::high});
    row.result.series.at(Gender::female, Income::high, "count").at(0) = 1.0;

    {
        ResultWriter writer{directory / "result.json", metadata(), true,
                            hgps::core::income_category_layout_from_config("5")};
        writer.write(row);
    }

    for (const auto *name : {"result_LowIncome.csv", "result_LowerMiddleIncome.csv",
                             "result_MiddleIncome.csv", "result_UpperMiddleIncome.csv",
                             "result_HighIncome.csv"}) {
        EXPECT_TRUE(std::filesystem::is_regular_file(directory / name)) << name << " is missing";
    }
}

TEST(ResultWriterTest, EveryStratumFileGetsTheYearEvenWhenOnlyOneHasPopulation) {
    // The baseline's HighIncomeFileGetsTimestepWhenOnlyHighHasPopulation, generalised: the
    // stratum files are a partition of the same run, so a year present in one is present in all.
    // The baseline decides which files exist by scanning the population for the categories
    // present, so a small cohort can lose a file entirely; here the configured layout decides.
    const auto directory = hgps::test::scratch_dir("writer_income_one_populated");
    auto row = row_with_income(2, {Income::low, Income::middle, Income::high});
    row.result.series.at(Gender::male, Income::high, "count").at(0) = 4.0;
    row.result.population_by_income->at(Income::high) = 4.0;

    {
        ResultWriter writer{directory / "result.json", metadata(), true,
                            hgps::core::income_category_layout_from_config("3")};
        writer.write(row);
    }

    for (const auto *name : {"result_LowIncome.csv", "result_MiddleIncome.csv",
                             "result_HighIncome.csv"}) {
        const auto lines = read_lines(directory / name);
        ASSERT_EQ(5U, lines.size()) << name; // header plus two ages times two sexes
        EXPECT_EQ("2022", field(lines[1], 2)) << name << " is missing the year";
    }
}

TEST(ResultWriterTest, WritesNoIncomeFilesWhenIncomeOutputIsOff) {
    const auto directory = hgps::test::scratch_dir("writer_income_off");
    auto row = row_with_income(1, {Income::low});
    row.result.series.at(Gender::male, Income::low, "count").at(0) = 1.0;

    {
        ResultWriter writer{directory / "result.json", metadata(), false,
                            hgps::core::income_category_layout_from_config("3")};
        writer.write(row);
    }

    EXPECT_TRUE(std::filesystem::is_regular_file(directory / "result.csv"));
    for (const auto &entry : std::filesystem::directory_iterator{directory}) {
        EXPECT_EQ(std::string::npos, entry.path().filename().string().find("Income"))
            << entry.path();
    }
}

TEST(ResultWriterTest, TheCsvCarriesNoTimestampAndTheMetadataCarriesTheSeedUsed) {
    // N-15 and N-16: two runs of one config must be byte-comparable, so the timestamp lives in
    // the JSON. B-06: the recorded seed is the seed the run used, and there is no unseeded run
    // for it to record as 0.
    const auto directory = hgps::test::scratch_dir("writer_metadata");
    auto row = row_with_income(1, {});
    row.result.series.at(Gender::male, "count").at(0) = 1.0;

    std::filesystem::path csv;
    std::filesystem::path metadata_path;
    {
        ResultWriter writer{directory / "result.json", metadata(), false,
                            hgps::core::income_category_layout_from_config("3")};
        writer.write(row);
        csv = writer.csv_path();
        metadata_path = writer.json_path();
    }

    std::ifstream csv_stream{csv};
    const std::string csv_text{std::istreambuf_iterator<char>{csv_stream},
                               std::istreambuf_iterator<char>{}};
    EXPECT_EQ(std::string::npos, csv_text.find("utc"));

    std::ifstream json_stream{metadata_path};
    const auto document = nlohmann::json::parse(json_stream);
    ASSERT_TRUE(document.contains("metadata"));
    const auto &recorded = document["metadata"];
    EXPECT_EQ(123456789U, recorded["seed"].get<std::uint32_t>());
    ASSERT_TRUE(recorded.contains("run_seeds"));
    EXPECT_EQ(987654321U, recorded["run_seeds"][0].get<std::uint32_t>());
    EXPECT_EQ(std::string(64, 'a'), recorded["config_sha256"].get<std::string>());
    EXPECT_TRUE(recorded.contains("started_utc"));
}

TEST(ResultWriterTest, RowsAreMaleThenFemaleForEachAgeInAscendingOrder) {
    // Determinism clause D10: the row order is the output contract, not an artefact of which
    // thread finished first (audit B-01).
    const auto directory = hgps::test::scratch_dir("writer_row_order");
    auto row = row_with_income(3, {});
    for (std::size_t age = 0; age < 3; ++age) {
        row.result.series.at(Gender::male, "count").at(age) = static_cast<double>(age + 1);
        row.result.series.at(Gender::female, "count").at(age) = static_cast<double>(age + 10);
    }

    {
        ResultWriter writer{directory / "result.json", metadata(), false,
                            hgps::core::income_category_layout_from_config("3")};
        writer.write(row);
    }

    const auto lines = read_lines(directory / "result.csv");
    ASSERT_EQ(7U, lines.size());

    const std::vector<std::pair<std::string, std::string>> expected{
        {"male", "0"}, {"female", "0"}, {"male", "1"},
        {"female", "1"}, {"male", "2"}, {"female", "2"}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i].first, field(lines[i + 1], 3)) << "line " << i + 1;
        EXPECT_EQ(expected[i].second, field(lines[i + 1], 4)) << "line " << i + 1;
    }
}
