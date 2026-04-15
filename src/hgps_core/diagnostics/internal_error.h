#pragma once

#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>

namespace hgps::core {

class InternalError : public std::runtime_error {
  public:
    InternalError(const std::string &what_arg,
                  const std::source_location location = std::source_location::current());

    constexpr std::uint_least32_t line() const noexcept;

    constexpr std::uint_least32_t column() const noexcept;

    constexpr const char *file_name() const noexcept;

    constexpr const char *function_name() const noexcept;

  private:
    std::source_location location_;
};

} // namespace hgps::core