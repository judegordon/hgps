#include "string_util.h"

#include "chars.h"

#include <algorithm>

namespace hgps::core {

std::string trim(std::string value) noexcept {
    while (!value.empty() && chars::is_space(value.back())) {
        value.pop_back();
    }

    std::size_t pos = 0;
    while (pos < value.size() && chars::is_space(value[pos])) {
        ++pos;
    }

    return value.substr(pos);
}

std::string to_lower(std::string_view value) noexcept {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return chars::to_lower(c); });
    return result;
}

std::string to_upper(std::string_view value) noexcept {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return chars::to_upper(c); });
    return result;
}

std::vector<std::string_view> split_string(std::string_view value,
                                           std::string_view delims) noexcept {
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

bool case_insensitive::comparator::operator()(std::string_view left, std::string_view right) const {
    return std::lexicographical_compare(
        left.cbegin(), left.cend(), right.cbegin(), right.cend(),
        [](char a, char b) { return chars::to_lower(a) < chars::to_lower(b); });
}

std::weak_ordering case_insensitive::compare(std::string_view left,
                                             std::string_view right) noexcept {
    const int cmp = to_lower(left).compare(to_lower(right));
    if (cmp < 0) {
        return std::weak_ordering::less;
    }
    if (cmp > 0) {
        return std::weak_ordering::greater;
    }
    return std::weak_ordering::equivalent;
}

bool case_insensitive::equals(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.cbegin(), left.cend(), right.cbegin(), right.cend(),
                      [](char a, char b) {
                          return a == b || chars::to_lower(a) == chars::to_lower(b);
                      });
}

bool case_insensitive::contains(std::string_view text, std::string_view str) noexcept {
    if (str.size() > text.size()) {
        return false;
    }
    const auto it = std::search(text.cbegin(), text.cend(), str.cbegin(), str.cend(),
                               [](char a, char b) {
                                   return chars::to_lower(a) == chars::to_lower(b);
                               });
    return it != text.cend();
}

bool case_insensitive::contains(const std::vector<std::string> &source,
                                std::string_view element) noexcept {
    return std::any_of(source.cbegin(), source.cend(),
                       [element](const auto &value) { return equals(value, element); });
}

bool case_insensitive::starts_with(std::string_view text, std::string_view str) noexcept {
    if (str.size() > text.size()) {
        return false;
    }
    return equals(text.substr(0, str.size()), str);
}

bool case_insensitive::ends_with(std::string_view text, std::string_view str) noexcept {
    if (str.size() > text.size()) {
        return false;
    }
    return equals(text.substr(text.size() - str.size()), str);
}

int case_insensitive::index_of(const std::vector<std::string> &source,
                               std::string_view element) noexcept {
    const auto it = std::find_if(source.cbegin(), source.cend(),
                                 [element](const std::string &other) {
                                     return equals(element, other);
                                 });
    if (it != source.cend()) {
        return static_cast<int>(std::distance(source.cbegin(), it));
    }
    return -1;
}

} // namespace hgps::core
