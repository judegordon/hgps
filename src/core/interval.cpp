#include "interval.h"

#include "string_util.h"

namespace hgps::core {
namespace {

// The baseline called std::stoi(parts[0].data()) on a string_view, which reads past the field:
// .data() is not null-terminated at the delimiter, so "10-20" parsed the leading digits only by
// luck of stoi stopping at '-'. Copying the field first makes the parse read exactly the field.
template <class Parse>
auto parse_pair(std::string_view value, std::string_view delims, Parse parse) {
    const auto parts = split_string(value, delims);
    if (parts.size() != 2) {
        throw std::invalid_argument(
            fmt::format("Input value:'{}' does not have the right format: xx-xx.", value));
    }

    return std::make_pair(parse(std::string{parts[0]}), parse(std::string{parts[1]}));
}

} // namespace

IntegerInterval parse_integer_interval(std::string_view value, std::string_view delims) {
    const auto [lower, upper] =
        parse_pair(value, delims, [](const std::string &s) { return std::stoi(s); });
    return IntegerInterval(lower, upper);
}

FloatInterval parse_float_interval(std::string_view value, std::string_view delims) {
    const auto [lower, upper] =
        parse_pair(value, delims, [](const std::string &s) { return std::stof(s); });
    return FloatInterval(lower, upper);
}

DoubleInterval parse_double_interval(std::string_view value, std::string_view delims) {
    const auto [lower, upper] =
        parse_pair(value, delims, [](const std::string &s) { return std::stod(s); });
    return DoubleInterval(lower, upper);
}

} // namespace hgps::core
