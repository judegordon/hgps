#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hgps::diag {

/// @brief How bad an input issue is.
enum class IssueLevel {
    /// @brief The run cannot proceed.
    error,
    /// @brief The run can proceed; this says what was assumed.
    warning,
};

/// @brief The closed set of input problems this program can report.
///
/// Closed, and an enum rather than free text, so that tests and tooling can act on an issue
/// instead of parsing prose. Adding a member is a deliberate act: it means a new class of user
/// mistake has been identified, and it needs a message and a test.
enum class IssueCode {
    // Files and JSON
    file_not_found,
    file_unreadable,
    json_parse_error,

    // Config document
    config_missing_required,
    config_unknown_property,
    config_wrong_type,
    config_bad_value,
    config_removed_property,
    config_undefined_variable,
    config_schema_mismatch,
    config_default_applied,

    // Risk-factor model definitions
    model_file_not_found,
    model_unknown_name,
    model_unknown_predictor,
    model_missing_key,
    model_bad_value,
    model_dimension_mismatch,
    model_default_applied,

    // CSV
    csv_empty,
    csv_missing_column,
    csv_duplicate_column,
    csv_ragged_row,
    csv_bad_value,

    // Data store
    data_index_invalid,
    data_missing_file,
    data_disease_not_in_tree,
    data_disease_not_in_registry,
    data_country_unknown,
    data_checksum_missing,
    data_checksum_mismatch,
    data_source_invalid,
    data_tool_missing,
    data_download_failed,
    data_extract_failed,

    // Scope
    feature_not_implemented,
};

/// @brief The name of an issue code as it appears in a report, e.g. "config_unknown_property".
std::string_view to_string(IssueCode code) noexcept;

/// @brief Where an issue was found. Every field is optional because not every issue has one.
struct IssueLocation {
    /// @brief The file the problem is in, as the user named it.
    std::string file{};

    /// @brief A JSON pointer ("/running/seed"), a CSV column name, or a field path.
    std::string field{};

    /// @brief 1-based line, when known.
    std::optional<std::size_t> line{};

    /// @brief 1-based column, when known.
    std::optional<std::size_t> column{};

    bool empty() const noexcept {
        return file.empty() && field.empty() && !line.has_value() && !column.has_value();
    }

    /// @brief "france.json:34:5 (/running/seed)", or as much of it as is known.
    std::string to_string() const;
};

/// @brief One problem with the user's input.
struct InputIssue {
    IssueLevel level{IssueLevel::error};
    IssueCode code{IssueCode::config_bad_value};
    IssueLocation location{};
    std::string message{};

    /// @brief One report line: "error  [code]  location  message".
    std::string to_string() const;
};

} // namespace hgps::diag
