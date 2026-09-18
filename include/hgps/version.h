// Part of the public API of hgps::engine. Nothing here includes an internal header.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
#pragma once

#include <string_view>

namespace hgps::api {

/// @brief What produced this binary, for a run manifest and for `--version`.
///
/// Every field is a compile-time constant baked in by the build, because a manifest that says
/// which commit produced a number is provenance and one that reads it from the environment at run
/// time is not. A field that could not be determined — a source tarball has no git metadata —
/// reads "unknown" rather than being empty.
struct BuildInfo {
    /// @brief The engine version, e.g. "0.2.0".
    std::string_view version;

    /// @brief The full commit hash, or "unknown".
    std::string_view git_commit;

    /// @brief `git describe --always --tags --dirty`, or "unknown".
    std::string_view git_describe;

    /// @brief True if the working tree had uncommitted changes to tracked files when configured.
    bool git_dirty;

    /// @brief "<system>-<processor>", e.g. "Darwin-arm64".
    std::string_view platform;

    /// @brief The compiler identification and version, e.g. "AppleClang 21.0.0.21000334".
    std::string_view compiler;

    /// @brief The CMake build type, e.g. "Release". Empty for a multi-config generator.
    std::string_view build_type;
};

/// @brief The build stamp. A reference to a static constant; never null, never changes.
const BuildInfo &build_info() noexcept;

/// @brief Shorthand for `build_info().version`.
std::string_view version() noexcept;

} // namespace hgps::api
