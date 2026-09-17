#include "chars.h"

#include <cctype>

namespace hgps::core::chars {
namespace {
// One cast, one place. Everything below funnels through this.
int as_byte(char c) noexcept { return static_cast<unsigned char>(c); }
} // namespace

bool is_digit(char c) noexcept { return std::isdigit(as_byte(c)) != 0; }

bool is_alpha(char c) noexcept { return std::isalpha(as_byte(c)) != 0; }

bool is_alnum(char c) noexcept { return std::isalnum(as_byte(c)) != 0; }

bool is_space(char c) noexcept { return std::isspace(as_byte(c)) != 0; }

char to_lower(char c) noexcept { return static_cast<char>(std::tolower(as_byte(c))); }

char to_upper(char c) noexcept { return static_cast<char>(std::toupper(as_byte(c))); }

} // namespace hgps::core::chars
