#pragma once

#include "diagnostics/issue_report.h"
#include "io/json.h"
#include "types.h"

#include <filesystem>
#include <optional>
#include <string>

namespace hgps::config {

/// @brief Things the command line can override or supply.
struct LoadOptions {
    /// @brief `--output` — allowed only when `output.folder` is empty, as upstream.
    std::optional<std::string> output_folder;

    /// @brief `--jobid` for HPC array jobs; 0 means "not a job".
    int job_id{0};

    bool verbose{false};

    /// @brief Whether a path named in the config must already exist. Off in tests that check
    ///        parsing rather than the file system.
    bool require_files_exist{true};
};

/// @brief Loads and validates a config v2 document.
///
/// Reports every problem it finds rather than stopping at the first, and returns nullopt if any
/// of them is an error. Warnings — an applied default, for instance — come back alongside a
/// usable config (docs/decisions/0007-two-tier-diagnostics.md).
std::optional<Config> load(const std::filesystem::path &path, const LoadOptions &options,
                           diag::IssueReport &report);

/// @brief Validates an already-parsed document, for tests and for the converter's output check.
std::optional<Config> load_from_json(const nlohmann::json &document,
                                     const std::filesystem::path &root_path,
                                     const LoadOptions &options, diag::IssueReport &report);

/// @brief The output file name, with `{TIMESTAMP}` and `{JOBID}` expanded if present.
///
/// The configured name is used exactly as given. `{TIMESTAMP}` is optional: the baseline ignores
/// the configured name entirely unless it contains a token (audit B-08), and a forced timestamp
/// means two runs of the same config never write comparable paths (N-15).
std::string expand_output_file_name(const Output &output, int job_id);

/// @brief The section loaders, exposed for testing one part of a document at a time.
namespace detail {

std::optional<ProjectRequirements> load_project_requirements(const io::JsonCursor &root,
                                                             diag::IssueReport &report);
std::optional<DataSpec> load_data(const io::JsonCursor &root, diag::IssueReport &report);
bool load_inputs(const io::JsonCursor &root, const std::filesystem::path &root_path,
                 const LoadOptions &options, Config &config, diag::IssueReport &report);
bool load_modelling(const io::JsonCursor &root, const std::filesystem::path &root_path,
                    const LoadOptions &options, Config &config, diag::IssueReport &report);
bool load_running(const io::JsonCursor &root, Config &config, diag::IssueReport &report);
bool load_output(const io::JsonCursor &root, const LoadOptions &options, Config &config,
                 diag::IssueReport &report);
bool check_version(const io::JsonCursor &root, diag::IssueReport &report);

/// @brief Resolves a path from the config against the config's directory, checking existence.
std::optional<std::filesystem::path> resolve_path(const io::JsonCursor &cursor,
                                                  const std::string &field,
                                                  const std::filesystem::path &root_path,
                                                  bool require_exists, diag::IssueReport &report);

} // namespace detail

} // namespace hgps::config
