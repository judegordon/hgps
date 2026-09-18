// Part of the public API of hgps::engine. Nothing here includes an internal header.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hgps::api {

/// @brief How bad a diagnostic is.
///
/// Two levels, not five. An `error` means the run cannot proceed; a `warning` says what was
/// assumed instead of stopping. Everything the engine has to tell a caller about their input fits
/// one of those, and a third level would only invite the question of whether it stops the run
/// (docs/decisions/0007-two-tier-diagnostics.md).
enum class Severity {
    warning,
    error,
};

/// @brief The name of a severity as it appears in a report: "warning" or "error".
std::string_view to_string(Severity severity) noexcept;

/// @brief Where a diagnostic was found. Every field is optional because not every issue has one.
struct Location {
    /// @brief The file the problem is in, as the caller named it.
    std::string file;

    /// @brief A JSON pointer ("/running/seed"), a CSV column name, or a field path.
    std::string field;

    /// @brief 1-based line, when known.
    std::optional<std::size_t> line;

    /// @brief 1-based column, when known.
    std::optional<std::size_t> column;

    bool empty() const noexcept;

    /// @brief "france.json:34:5 (/running/seed)", or as much of it as is known.
    std::string to_string() const;
};

/// @brief One problem with the caller's input, or one thing the engine assumed.
struct Diagnostic {
    Severity severity{Severity::error};

    /// @brief A stable machine-readable code, e.g. "config_bad_value".
    ///
    /// A string rather than an enumeration, so that adding a code inside the engine does not
    /// change this header — and so a caller can switch on it without recompiling against a new
    /// enumerator it does not know.
    std::string code;

    Location location;
    std::string message;

    /// @brief One report line: "error  [code]  location  message".
    std::string to_string() const;
};

/// @brief Every diagnostic one call accumulated, in the order they were found.
///
/// The engine reports everything wrong with an input rather than stopping at the first problem, so
/// a caller gets one list to show a user instead of a dialogue of one mistake per run. A load that
/// failed and a load that succeeded with warnings are told apart by `has_errors()`, not by whether
/// the report is empty.
class Report {
  public:
    const std::vector<Diagnostic> &diagnostics() const noexcept { return diagnostics_; }

    bool empty() const noexcept { return diagnostics_.empty(); }
    bool has_errors() const noexcept { return error_count_ > 0; }
    std::size_t error_count() const noexcept { return error_count_; }
    std::size_t warning_count() const noexcept { return warning_count_; }

    /// @brief True if any diagnostic carries this code, at either severity.
    bool contains(std::string_view code) const noexcept;

    void add(Diagnostic diagnostic);

    /// @brief Appends another report's diagnostics, preserving their order.
    void merge(const Report &other);

    /// @brief Every diagnostic, one per line, followed by a count summary.
    std::string to_string() const;

    void clear() noexcept;

  private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t error_count_{0};
    std::size_t warning_count_{0};
};

} // namespace hgps::api
