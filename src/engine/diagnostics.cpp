#include "hgps/diagnostics.h"

#include "diagnostics_bridge.h"

#include <algorithm>

#include <fmt/format.h>

namespace hgps::api {

std::string_view to_string(Severity severity) noexcept {
    return severity == Severity::error ? "error" : "warning";
}

bool Location::empty() const noexcept {
    return file.empty() && field.empty() && !line.has_value() && !column.has_value();
}

std::string Location::to_string() const {
    std::string result = file;
    if (line.has_value()) {
        result += fmt::format(":{}", *line);
        if (column.has_value()) {
            result += fmt::format(":{}", *column);
        }
    }
    if (!field.empty()) {
        if (!result.empty()) {
            result += ' ';
        }
        result += fmt::format("({})", field);
    }
    return result;
}

std::string Diagnostic::to_string() const {
    const auto where = location.to_string();
    return fmt::format("{:<7} [{}]  {}{}{}", api::to_string(severity), code, where,
                       where.empty() ? "" : "  ", message);
}

bool Report::contains(std::string_view code) const noexcept {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(),
                       [code](const Diagnostic &diagnostic) { return diagnostic.code == code; });
}

void Report::add(Diagnostic diagnostic) {
    if (diagnostic.severity == Severity::error) {
        ++error_count_;
    } else {
        ++warning_count_;
    }
    diagnostics_.push_back(std::move(diagnostic));
}

void Report::merge(const Report &other) {
    for (const auto &diagnostic : other.diagnostics_) {
        add(diagnostic);
    }
}

std::string Report::to_string() const {
    std::string result;
    for (const auto &diagnostic : diagnostics_) {
        result += diagnostic.to_string();
        result += '\n';
    }
    if (!diagnostics_.empty()) {
        result += fmt::format("{} error{}, {} warning{}\n", error_count_,
                              error_count_ == 1 ? "" : "s", warning_count_,
                              warning_count_ == 1 ? "" : "s");
    }
    return result;
}

void Report::clear() noexcept {
    diagnostics_.clear();
    error_count_ = 0;
    warning_count_ = 0;
}

namespace detail {

Diagnostic to_public(const diag::InputIssue &issue) {
    return Diagnostic{
        .severity = issue.level == diag::IssueLevel::error ? Severity::error : Severity::warning,
        .code = std::string{diag::to_string(issue.code)},
        .location = Location{.file = issue.location.file,
                             .field = issue.location.field,
                             .line = issue.location.line,
                             .column = issue.location.column},
        .message = issue.message,
    };
}

void append_to_public(const diag::IssueReport &internal, Report &report) {
    for (const auto &issue : internal.issues()) {
        report.add(to_public(issue));
    }
}

} // namespace detail

} // namespace hgps::api
