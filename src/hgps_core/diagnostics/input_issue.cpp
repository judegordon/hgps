#include "input_issue.h"

#include <sstream>
#include <utility>

namespace hgps::core {

std::string_view to_string(IssueLevel level) noexcept {
    switch (level) {
    case IssueLevel::warning:
        return "warning";
    case IssueLevel::error:
        return "error";
    }

    return "unknown";
}

std::string_view to_string(IssueCode code) noexcept {
    switch (code) {
    case IssueCode::unknown:
        return "unknown";
    case IssueCode::missing_key:
        return "missing_key";
    case IssueCode::wrong_type:
        return "wrong_type";
    case IssueCode::invalid_value:
        return "invalid_value";
        case IssueCode::invalid_enum_value:
        return "invalid_enum_value";
    case IssueCode::missing_file:
        return "missing_file";
    case IssueCode::invalid_path:
        return "invalid_path";
    case IssueCode::parse_failure:
        return "parse_failure";
    case IssueCode::missing_column:
        return "missing_column";
    case IssueCode::duplicate_entry:
        return "duplicate_entry";
    }

    return "unknown";
}

void InputIssueReport::add(InputIssue issue) {
    if (issue.level == IssueLevel::error) {
        ++error_count_;
    } else {
        ++warning_count_;
    }

    items_.push_back(std::move(issue));
}

void InputIssueReport::add(IssueLevel level, IssueCode code, IssueLocation location,
                           std::string message) {
    add(InputIssue{level, code, std::move(location), std::move(message)});
}

void InputIssueReport::error(IssueCode code, IssueLocation location, std::string message) {
    add(IssueLevel::error, code, std::move(location), std::move(message));
}

void InputIssueReport::warning(IssueCode code, IssueLocation location, std::string message) {
    add(IssueLevel::warning, code, std::move(location), std::move(message));
}

bool InputIssueReport::empty() const noexcept {
    return items_.empty();
}

bool InputIssueReport::has_errors() const noexcept {
    return error_count_ != 0;
}

bool InputIssueReport::has_warnings() const noexcept {
    return warning_count_ != 0;
}

std::size_t InputIssueReport::size() const noexcept {
    return items_.size();
}

std::size_t InputIssueReport::error_count() const noexcept {
    return error_count_;
}

std::size_t InputIssueReport::warning_count() const noexcept {
    return warning_count_;
}

const InputIssueReport::container_type &InputIssueReport::all() const noexcept {
    return items_;
}

InputIssueReport::const_iterator InputIssueReport::begin() const noexcept {
    return items_.begin();
}

InputIssueReport::const_iterator InputIssueReport::end() const noexcept {
    return items_.end();
}

void InputIssueReport::clear() noexcept {
    items_.clear();
    error_count_ = 0;
    warning_count_ = 0;
}

std::string format_issue(const InputIssue &issue) {
    std::ostringstream stream;

    stream << '[' << to_string(issue.level) << ']'
           << '[' << to_string(issue.code) << ']';

    if (issue.location.has_source_path()) {
        stream << ' ' << issue.location.source_path;
    }

    if (issue.location.has_field_path()) {
        stream << " (" << issue.location.field_path << ')';
    }

    if (issue.location.has_line()) {
        stream << ':' << issue.location.line;
        if (issue.location.has_column()) {
            stream << ':' << issue.location.column;
        }
    }

    if (!issue.message.empty()) {
        stream << " - " << issue.message;
    }

    return stream.str();
}

} // namespace hgps::core