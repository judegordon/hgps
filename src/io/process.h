#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace hgps::io {

/// @brief The outcome of running an external program.
struct ProcessResult {
    int exit_code{-1};

    /// @brief Whether the program could be started at all.
    bool started{false};

    /// @brief Combined stdout and stderr, truncated to a few kilobytes: enough for a diagnostic,
    ///        not enough to flood a report with a progress bar.
    std::string output;

    bool succeeded() const noexcept { return started && exit_code == 0; }
};

/// @brief Runs a program with an explicit argument vector. No shell is involved, so nothing in
///        `arguments` is interpreted — a URL with a semicolon in it is a URL.
///
/// Used for the two things this project delegates rather than links: `curl` to download a data
/// archive and `unzip` to extract one (docs/decisions/0014-minimal-dependency-set.md). The
/// checksum of whatever comes back is computed by our own code, so a compromised or confused tool
/// cannot substitute data unnoticed.
ProcessResult run_process(const std::string &program, const std::vector<std::string> &arguments);

/// @brief Whether an executable of this name can be found on PATH.
bool tool_available(const std::string &program);

} // namespace hgps::io
