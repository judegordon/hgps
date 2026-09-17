#pragma once

#include "config/types.h"
#include "diagnostics/issue_report.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hgps::test {

/// @brief What one harness run produced.
struct RunOutcome {
    bool succeeded{false};
    hgps::diag::IssueReport report;
    std::filesystem::path csv_path;
    std::filesystem::path json_path;
    std::vector<std::filesystem::path> all_paths;
};

/// @brief Runs a whole simulation in process, through the same code path as the CLI.
///
/// Everything but argument parsing: config loading, data resolution, model loading, the engine,
/// the runner and the writer. That is what makes the reproducibility test meaningful — it
/// exercises the real pipeline rather than a test-only arrangement of it.
///
/// @param config_path The config v2 file to run.
/// @param output_folder Where results go; the config's own output.folder is overridden.
/// @param threads Workers for the RNG-free parallel sections.
RunOutcome run_simulation(const std::filesystem::path &config_path,
                          const std::filesystem::path &output_folder, std::size_t threads = 1);

/// @brief The synthetic config as JSON, for a test that needs to change something in it.
nlohmann::json synthetic_config_document();

/// @brief Writes a config document into a directory that already holds the model pack's files,
///        so relative paths still resolve. Returns the new config's path.
///
/// The model pack's directory is read-only for tests, so the pack is copied.
std::filesystem::path write_config_variant(const std::string &test_name,
                                           const nlohmann::json &document);

} // namespace hgps::test
