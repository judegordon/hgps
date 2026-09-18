#pragma once

#include "hgps/engine.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hgps::test {

/// @brief What one harness run produced.
struct RunOutcome {
    bool succeeded{false};
    hgps::api::Report report;
    std::filesystem::path csv_path;
    std::filesystem::path json_path;
    std::filesystem::path manifest_path;
    std::vector<std::filesystem::path> all_paths;

    /// @brief Set when the run was cancelled before the horizon.
    bool cancelled{false};

    /// @brief How many scenario-years were simulated.
    std::size_t years_completed{0};
};

/// @brief Runs a whole simulation in process, through the public API and nothing else.
///
/// This used to be a copy of `main()`'s body, which is why the library/CLI split exists: the
/// sequence of load, resolve, build and execute belongs to the engine, and this function is now
/// four calls to it. A test that passes here therefore exercises exactly what a caller — the CLI,
/// or a GUI — gets (docs/decisions/0032-library-and-a-thin-cli.md).
///
/// @param config_path The config v2 file to run.
/// @param output_folder Where results go; the config's own output.folder is overridden.
/// @param threads Workers for the RNG-free parallel sections.
/// @param subscriber Progress events, or null for none.
/// @param cancellation Checked between years; the default token is never cancelled.
RunOutcome run_simulation(const std::filesystem::path &config_path,
                          const std::filesystem::path &output_folder, std::size_t threads = 1,
                          hgps::api::EventSubscriber *subscriber = nullptr,
                          const hgps::api::CancellationToken &cancellation = {});

/// @brief `run_simulation` with a perturbation applied. See `hgps::api::RunOptions::perturbation`.
RunOutcome run_simulation_perturbed(const std::filesystem::path &config_path,
                                    const std::filesystem::path &output_folder,
                                    const std::string &perturbation);

/// @brief The synthetic config as JSON, for a test that needs to change something in it.
nlohmann::json synthetic_config_document();

/// @brief Writes a config document into a directory that already holds the model pack's files,
///        so relative paths still resolve. Returns the new config's path.
///
/// The model pack's directory is read-only for tests, so the pack is copied.
std::filesystem::path write_config_variant(const std::string &test_name,
                                           const nlohmann::json &document);

} // namespace hgps::test
