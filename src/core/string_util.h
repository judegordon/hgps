// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/string_util.h, string_util.cpp.
#pragma once

#include <compare>
#include <string>
#include <string_view>
#include <vector>

namespace hgps::core {

/// @brief Removes leading and trailing whitespace.
std::string trim(std::string value) noexcept;

/// @brief Lower-cases an ASCII string. Bytes >= 0x80 are passed through unchanged by
///        core::chars::to_lower rather than being fed to <cctype> as a negative int.
std::string to_lower(std::string_view value) noexcept;

/// @brief Upper-cases an ASCII string.
std::string to_upper(std::string_view value) noexcept;

/// @brief Splits on any of the delimiter characters, dropping empty fields.
/// @note Empty fields are dropped, which is what the baseline does and what interval parsing
///       ("0-100") relies on. CSV parsing needs the opposite and uses io::CsvReader instead.
std::vector<std::string_view> split_string(std::string_view value,
                                           std::string_view delims) noexcept;

/// @brief Case-insensitive operations on ASCII strings.
struct case_insensitive final {
    /// @brief Strict weak ordering, for use as a container comparator.
    struct comparator {
        bool operator()(std::string_view left, std::string_view right) const;
    };

    static std::weak_ordering compare(std::string_view left, std::string_view right) noexcept;
    static bool equals(std::string_view left, std::string_view right) noexcept;
    static bool contains(std::string_view text, std::string_view str) noexcept;
    static bool contains(const std::vector<std::string> &source, std::string_view element) noexcept;
    static bool starts_with(std::string_view text, std::string_view str) noexcept;
    static bool ends_with(std::string_view text, std::string_view str) noexcept;

    /// @brief Zero-based index of the first case-insensitive match, or -1.
    static int index_of(const std::vector<std::string> &source, std::string_view element) noexcept;
};

} // namespace hgps::core
