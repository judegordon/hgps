#include "issue.h"

#include <fmt/format.h>

namespace hgps::diag {

std::string_view to_string(IssueCode code) noexcept {
    switch (code) {
    case IssueCode::file_not_found:
        return "file_not_found";
    case IssueCode::file_unreadable:
        return "file_unreadable";
    case IssueCode::json_parse_error:
        return "json_parse_error";
    case IssueCode::config_missing_required:
        return "config_missing_required";
    case IssueCode::config_unknown_property:
        return "config_unknown_property";
    case IssueCode::config_wrong_type:
        return "config_wrong_type";
    case IssueCode::config_bad_value:
        return "config_bad_value";
    case IssueCode::config_removed_property:
        return "config_removed_property";
    case IssueCode::config_undefined_variable:
        return "config_undefined_variable";
    case IssueCode::config_schema_mismatch:
        return "config_schema_mismatch";
    case IssueCode::config_default_applied:
        return "config_default_applied";
    case IssueCode::model_file_not_found:
        return "model_file_not_found";
    case IssueCode::model_unknown_name:
        return "model_unknown_name";
    case IssueCode::model_unknown_predictor:
        return "model_unknown_predictor";
    case IssueCode::model_missing_key:
        return "model_missing_key";
    case IssueCode::model_bad_value:
        return "model_bad_value";
    case IssueCode::model_dimension_mismatch:
        return "model_dimension_mismatch";
    case IssueCode::model_default_applied:
        return "model_default_applied";
    case IssueCode::csv_empty:
        return "csv_empty";
    case IssueCode::csv_missing_column:
        return "csv_missing_column";
    case IssueCode::csv_duplicate_column:
        return "csv_duplicate_column";
    case IssueCode::csv_ragged_row:
        return "csv_ragged_row";
    case IssueCode::csv_bad_value:
        return "csv_bad_value";
    case IssueCode::data_index_invalid:
        return "data_index_invalid";
    case IssueCode::data_missing_file:
        return "data_missing_file";
    case IssueCode::data_disease_not_in_tree:
        return "data_disease_not_in_tree";
    case IssueCode::data_disease_not_in_registry:
        return "data_disease_not_in_registry";
    case IssueCode::data_country_unknown:
        return "data_country_unknown";
    case IssueCode::data_checksum_missing:
        return "data_checksum_missing";
    case IssueCode::data_checksum_mismatch:
        return "data_checksum_mismatch";
    case IssueCode::data_source_invalid:
        return "data_source_invalid";
    case IssueCode::data_tool_missing:
        return "data_tool_missing";
    case IssueCode::data_download_failed:
        return "data_download_failed";
    case IssueCode::data_extract_failed:
        return "data_extract_failed";
    case IssueCode::feature_not_implemented:
        return "feature_not_implemented";
    }

    return "unknown";
}

std::string IssueLocation::to_string() const {
    std::string result = file;

    if (line.has_value()) {
        result += fmt::format(":{}", *line);
        if (column.has_value()) {
            result += fmt::format(":{}", *column);
        }
    }

    if (!field.empty()) {
        result += result.empty() ? field : fmt::format(" ({})", field);
    }

    return result;
}

std::string InputIssue::to_string() const {
    const std::string_view level_text = level == IssueLevel::error ? "error" : "warning";
    const std::string where = location.to_string();

    if (where.empty()) {
        return fmt::format("{:<7} [{}] {}", level_text, diag::to_string(code), message);
    }

    return fmt::format("{:<7} [{}] {}: {}", level_text, diag::to_string(code), where, message);
}

} // namespace hgps::diag
