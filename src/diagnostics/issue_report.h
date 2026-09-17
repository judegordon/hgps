#pragma once

#include "issue.h"

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace hgps::diag {

/// @brief Accumulates input issues so the user sees all of them at once, each located.
///
/// Passed by non-const reference through config loading, model loading, CSV reading and data
/// resolution. Nothing here throws: a loader records what is wrong and keeps going as far as it
/// usefully can, and the caller checks has_errors() at the points named in docs/design.md
/// section 3.
class IssueReport {
  public:
    void add(InputIssue issue);

    /// @brief Records an error.
    void error(IssueCode code, IssueLocation location, std::string message);

    /// @brief Records a warning: the run can continue, and this says what was assumed.
    void warning(IssueCode code, IssueLocation location, std::string message);

    const std::vector<InputIssue> &issues() const noexcept { return issues_; }

    bool empty() const noexcept { return issues_.empty(); }
    bool has_errors() const noexcept { return error_count_ > 0; }
    std::size_t error_count() const noexcept { return error_count_; }
    std::size_t warning_count() const noexcept { return warning_count_; }

    /// @brief True if any recorded issue has this code, at any level. For tests and for callers
    ///        that need to know whether a specific class of problem occurred.
    bool contains(IssueCode code) const noexcept;

    /// @brief Appends another report's issues, preserving their order.
    void merge(const IssueReport &other);

    /// @brief Every issue, one per line, followed by a count summary.
    std::string to_string() const;

    void clear() noexcept;

  private:
    std::vector<InputIssue> issues_{};
    std::size_t error_count_{0};
    std::size_t warning_count_{0};
};

std::ostream &operator<<(std::ostream &stream, const IssueReport &report);

} // namespace hgps::diag
