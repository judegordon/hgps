#include "issue_report.h"

#include <algorithm>
#include <utility>

#include <fmt/format.h>

namespace hgps::diag {

void IssueReport::add(InputIssue issue) {
    if (issue.level == IssueLevel::error) {
        ++error_count_;
    } else {
        ++warning_count_;
    }

    issues_.push_back(std::move(issue));
}

void IssueReport::error(IssueCode code, IssueLocation location, std::string message) {
    add(InputIssue{.level = IssueLevel::error,
                   .code = code,
                   .location = std::move(location),
                   .message = std::move(message)});
}

void IssueReport::warning(IssueCode code, IssueLocation location, std::string message) {
    add(InputIssue{.level = IssueLevel::warning,
                   .code = code,
                   .location = std::move(location),
                   .message = std::move(message)});
}

bool IssueReport::contains(IssueCode code) const noexcept {
    return std::any_of(issues_.begin(), issues_.end(),
                       [code](const InputIssue &issue) { return issue.code == code; });
}

void IssueReport::merge(const IssueReport &other) {
    for (const auto &issue : other.issues_) {
        add(issue);
    }
}

std::string IssueReport::to_string() const {
    std::string result;
    for (const auto &issue : issues_) {
        result += issue.to_string();
        result += '\n';
    }

    result += fmt::format("{} error{}, {} warning{}\n", error_count_, error_count_ == 1 ? "" : "s",
                          warning_count_, warning_count_ == 1 ? "" : "s");
    return result;
}

void IssueReport::clear() noexcept {
    issues_.clear();
    error_count_ = 0;
    warning_count_ = 0;
}

std::ostream &operator<<(std::ostream &stream, const IssueReport &report) {
    stream << report.to_string();
    return stream;
}

} // namespace hgps::diag
