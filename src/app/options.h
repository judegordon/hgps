// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Console/command_options.{h,cpp}, which uses cxxopts.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hgps::app {

/// @brief What the command line asked for.
struct Options {
    std::filesystem::path config;
    std::optional<std::string> output_folder;

    /// @brief Validate everything and stop before simulating.
    bool dry_run{false};

    /// @brief Workers for the RNG-free parallel sections. The default is one, and the output is
    ///        byte-identical either way.
    std::size_t threads{1};

    int job_id{0};
    bool verbose{false};

    /// @brief Print a line as each simulated year finishes, and a header before the run.
    ///
    /// Off by default. The engine emits the events either way — they cost a function call per year
    /// and cannot change a result — and this only decides whether they are printed.
    bool progress{false};

    /// @brief Print the help text and exit.
    bool help{false};

    /// @brief Print the version and exit.
    bool version{false};

    /// @brief Test-only: corrupt named output channels. Empty in every ordinary run.
    ///
    /// It exists so the equivalence harness can check that it fails where it should
    /// (docs/equivalence-method.md §7.2). A run that uses it records the specification in its
    /// manifest, so its output cannot be mistaken for a real one.
    std::string perturb;
};

/// @brief What parsing produced: options, or a message explaining what was wrong.
struct OptionsResult {
    std::optional<Options> options;
    std::string message;
};

/// @brief Parses the command line.
///
/// Hand-written rather than cxxopts: it is a few dozen lines, the error messages are ours, and it
/// is one fewer dependency (docs/decisions/0014-minimal-dependency-set.md).
OptionsResult parse_options(const std::vector<std::string> &arguments);

/// @brief The help text.
std::string usage_text();

} // namespace hgps::app
