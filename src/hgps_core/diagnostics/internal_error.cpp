#include "internal_error.h"

#include <fmt/format.h>

namespace hgps::core {

InternalError::InternalError(const std::string &what_arg, const std::source_location location)
    : std::runtime_error{fmt::format("{}:{}: {}", location.file_name(), location.line(), what_arg)},
      location_{location} {}

constexpr std::uint_least32_t InternalError::line() const noexcept { return location_.line(); }

constexpr std::uint_least32_t InternalError::column() const noexcept { return location_.column(); }

constexpr const char *InternalError::file_name() const noexcept { return location_.file_name(); }

constexpr const char *InternalError::function_name() const noexcept {
    return location_.function_name();
}

} // namespace hgps::core