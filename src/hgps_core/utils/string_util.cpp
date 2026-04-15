// string_util.cpp
#include "string_util.h"

#include <algorithm>
#include <cctype>

namespace hgps::core {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    std::size_t pos = 0;
    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos]))) {
        ++pos;
    }

    return value.substr(pos);
}

std::string to_lower(const std::string_view& value) {
    std::string result(value);
    std::transform(value.begin(), value.end(), result.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });

    return result;
}

std::string to_upper(const std::string_view& value) {
    std::string result(value);
    std::transform(value.begin(), value.end(), result.begin(), [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });

    return result;
}

std::vector<std::string_view> split_string(const std::string_view& value, std::string_view delims) {
    std::vector<std::string_view> output;
    std::size_t first = 0;

    while (first < value.size()) {
        const auto second = value.find_first_of(delims, first);
        if (first != second) {
            output.emplace_back(value.substr(first, second - first));
        }

        if (second == std::string_view::npos) {
            break;
        }

        first = second + 1;
    }

    return output;
}

bool case_insensitive::comparator::operator()(const std::string_view& left,
                                              const std::string_view& right) const {
    return std::lexicographical_compare(
        left.cbegin(), left.cend(), right.cbegin(), right.cend(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) <
                   std::tolower(static_cast<unsigned char>(b));
        });
}

std::weak_ordering case_insensitive::compare(const std::string_view& left,
                                             const std::string_view& right) noexcept {
    int cmp = to_lower(left).compare(to_lower(right));
    if (cmp < 0) {
        return std::weak_ordering::less;
    }
    if (cmp > 0) {
        return std::weak_ordering::greater;
    }

    return std::weak_ordering::equivalent;
}

bool case_insensitive::equal_char(char left, char right) noexcept {
    return left == right ||
           std::tolower(static_cast<unsigned char>(left)) ==
               std::tolower(static_cast<unsigned char>(right));
}

bool case_insensitive::equals(const std::string_view& left,
                              const std::string_view& right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.cbegin(), left.cend(), right.cbegin(), right.cend(), equal_char);
}

bool case_insensitive::contains(const std::string_view& text,
                                const std::string_view& str) noexcept {
    if (str.size() > text.size()) {
        return false;
    }

    const auto it = std::search(text.cbegin(), text.cend(), str.cbegin(), str.cend(),
                                [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });

    return it != text.cend();
}

bool case_insensitive::starts_with(const std::string_view& text,
                                   const std::string_view& str) noexcept {
    if (str.size() > text.size()) {
        return false;
    }

    const auto it =
        std::search(text.cbegin(), text.cbegin() + str.length(), str.cbegin(), str.cend(),
                    [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) ==
                               std::tolower(static_cast<unsigned char>(b));
                    });

    return it == text.cbegin();
}

bool case_insensitive::ends_with(const std::string_view& text,
                                 const std::string_view& str) noexcept {
    if (str.size() > text.size()) {
        return false;
    }

    const auto it =
        std::search(text.crbegin(), text.crbegin() + str.length(), str.crbegin(), str.crend(),
                    [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) ==
                               std::tolower(static_cast<unsigned char>(b));
                    });

    return it == text.crbegin();
}

bool case_insensitive::contains(const std::vector<std::string>& source,
                                const std::string_view& element) noexcept {
    return std::any_of(source.cbegin(), source.cend(), [&element](const auto& value) {
        return core::case_insensitive::equals(value, element);
    });
}

int case_insensitive::index_of(const std::vector<std::string>& source,
                               const std::string_view& element) noexcept {
    const auto it =
        std::find_if(source.cbegin(), source.cend(), [&element](const std::string& other) {
            return case_insensitive::equals(element, other);
        });

    if (it != source.cend()) {
        return static_cast<int>(std::distance(source.cbegin(), it));
    }

    return -1;
}

} // namespace hgps::core