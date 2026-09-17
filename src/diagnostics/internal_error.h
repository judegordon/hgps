#pragma once

#include <source_location>
#include <stdexcept>
#include <string>

namespace hgps::diag {

/// @brief A programmer error: a broken invariant, an impossible state, a contract violated by a
///        caller inside this codebase.
///
/// Thrown, carries the source location of the throw site, and caught only at the top of main.
/// User input problems are NOT this type — they are accumulated as InputIssue, because a user
/// with five config mistakes should see five located messages rather than one stack unwind
/// (docs/decisions/0007-two-tier-diagnostics.md).
class InternalError : public std::runtime_error {
  public:
    explicit InternalError(const std::string &message,
                           std::source_location location = std::source_location::current());

    std::uint_least32_t line() const noexcept { return location_.line(); }
    std::uint_least32_t column() const noexcept { return location_.column(); }
    const char *file_name() const noexcept { return location_.file_name(); }
    const char *function_name() const noexcept { return location_.function_name(); }

    /// @brief "message [file:line function]", for the top-level handler.
    std::string describe() const;

  private:
    std::source_location location_;
};

} // namespace hgps::diag
