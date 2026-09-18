#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace hgps::io {

/// @brief The full path of the running executable.
///
/// The one platform branch in this project: _NSGetExecutablePath on macOS, /proc/self/exe on
/// Linux. The baseline has `#error "Unsupported platform"` here, which is why it does not build
/// on macOS at all (audit B-10, docs/decisions/0013-platforms-linux-and-macos.md).
///
/// @throws std::runtime_error if the path cannot be determined.
std::filesystem::path program_path();

/// @brief The directory holding the running executable.
std::filesystem::path program_directory();

/// @brief The user's cache directory for this program.
///
/// $XDG_CACHE_HOME/healthgps or ~/.cache/healthgps on Linux, ~/Library/Caches/healthgps on macOS.
/// Overridden by $HEALTHGPS_CACHE_DIR, which is what the tests and CI use so that a test run
/// never writes to a developer's real cache.
std::filesystem::path cache_directory();

/// @brief A temporary directory for this program, created if needed.
std::filesystem::path temporary_directory();

/// @brief Expands ${VAR} references using the environment.
///
/// @param value The string to expand.
/// @param undefined Receives the names of any variables that are not set. The baseline expanded
///        an undefined variable to the empty string silently, which quietly relocates a run's
///        output (audit N-17); the caller reports these as errors.
/// @return The expanded string, with undefined variables left as they were written.
std::string expand_environment_variables(const std::string &value,
                                         std::vector<std::string> &undefined);

} // namespace hgps::io
