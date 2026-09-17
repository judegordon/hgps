#pragma once

#include <string_view>

namespace hgps::core::chars {

// The <cctype> functions are defined only for arguments representable as unsigned char (or EOF).
// char is signed on every platform this project targets, so passing a byte >= 0x80 straight to
// them is undefined behaviour — the baseline does exactly that at eleven call sites (audit B-03),
// reachable from any non-ASCII byte in a CSV header, a country name or an identifier.
//
// These wrappers are the only place in this codebase that calls those functions. They take char
// and cast internally. Determinism contract clause D12; docs/decisions/0025-identifier-string-equality.md.

bool is_digit(char c) noexcept;
bool is_alpha(char c) noexcept;
bool is_alnum(char c) noexcept;
bool is_space(char c) noexcept;
char to_lower(char c) noexcept;
char to_upper(char c) noexcept;

} // namespace hgps::core::chars
