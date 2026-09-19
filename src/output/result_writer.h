// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Console/result_file_writer.{h,cpp}, model_info.h.
#pragma once

#include "core/income_category_layout.h"
#include "hgps/engine.h"
#include "sim/engine.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hgps::output {

/// The output families, and the rule that classifies a file into one, live in the **public** API
/// header: they are part of what a caller is told about a run, and the local server — which is
/// held to the public surface and nothing internal (tests/app/cli_boundary_test.cpp) — needs them
/// to offer one summary per file family. They are re-exported here so this writer and its tests
/// can spell them beside the code that produces the names.
using api::OutputFamily;
using api::all_output_families;
using api::output_family_name;
using api::output_family_of;

/// @brief What produced a result file, for its metadata.
struct RunMetadata {
    std::string model;
    std::string version;

    /// @brief The active intervention's identifier, or "" for a baseline-only run.
    std::string intervention;

    int job_id{};

    /// @brief The master seed the run used. Not "the seed the config might have had": the
    ///        baseline records `seed().value_or(0)`, which is wrong in exactly the case where it
    ///        matters (audit B-06).
    std::uint32_t seed{};

    /// @brief Each trial run's derived seed, so any single run can be reproduced on its own.
    std::vector<std::uint32_t> run_seeds;

    std::filesystem::path config_path;
    std::string config_sha256;

    std::string country;
    unsigned int start_time{};
    unsigned int stop_time{};
    unsigned int trial_runs{};
    std::size_t cohort_size{};
};

/// @brief Writes the result CSV, the income-stratified CSVs and the metadata JSON.
///
/// One owner per file, never shared between threads, rows emitted in the order the runner hands
/// them over — scenario, then run, then year, then sex, then age. That order is the output
/// contract (docs/decisions/0020-output-single-owner-defined-row-order.md): the baseline's rows
/// arrive in thread-completion order, so three same-seed runs produce three different files
/// (audit B-01).
///
/// The CSV carries no timestamp, so two runs of the same config are byte-comparable. The
/// timestamp lives in the JSON metadata (N-15, N-16).
class ResultWriter {
  public:
    ResultWriter() = delete;

    /// @param base_path The result file, with its extension. The CSV takes this name with a .csv
    ///        extension; the metadata takes it with .json; the income files take
    ///        `<stem>_<Category>.csv`.
    /// @throws std::invalid_argument if a file cannot be opened for writing.
    ResultWriter(std::filesystem::path base_path, RunMetadata metadata, bool write_income_files,
                 core::IncomeCategoryLayout income_layout);

    ~ResultWriter();
    ResultWriter(const ResultWriter &) = delete;
    ResultWriter &operator=(const ResultWriter &) = delete;
    ResultWriter(ResultWriter &&) = delete;
    ResultWriter &operator=(ResultWriter &&) = delete;

    /// @brief Writes one year of one scenario.
    void write(const sim::ResultRow &row);

    /// @brief Finishes the metadata JSON and closes every file. Called by the destructor too.
    void close();

    const std::filesystem::path &csv_path() const noexcept { return csv_path_; }
    const std::filesystem::path &json_path() const noexcept { return json_path_; }

    /// @brief The files this writer has opened, for the run summary.
    std::vector<std::filesystem::path> paths() const;

  private:
    std::filesystem::path csv_path_;
    std::filesystem::path json_path_;
    RunMetadata metadata_;
    bool write_income_files_;
    core::IncomeCategoryLayout income_layout_;

    std::ofstream csv_;
    std::ofstream json_;
    std::map<core::Income, std::ofstream> income_csv_;
    std::map<core::Income, std::filesystem::path> income_paths_;

    bool csv_header_written_{false};
    bool json_first_entry_{true};
    bool closed_{false};

    void write_csv_header(const model::DataSeries &series);
    void write_csv_rows(const sim::ResultRow &row);
    void write_income_rows(const sim::ResultRow &row);
    void write_json_begin();
    void write_json_entry(const sim::ResultRow &row);
    void write_json_end();
};

} // namespace hgps::output
