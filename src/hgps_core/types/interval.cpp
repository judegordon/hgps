// interval.cpp
#include "interval.h"

#include <stdexcept>
#include <string>

namespace hgps::core {

IntegerInterval parse_integer_interval(const std::string_view& value,
                                       const std::string_view delims) {
    const auto parts = split_string(value, delims);
    if (parts.size() == 2) {
        return IntegerInterval(std::stoi(std::string(parts[0])),
                               std::stoi(std::string(parts[1])));
    }

    throw std::invalid_argument(
        fmt::format("Input value:'{}' does not have the right format: xx-xx.", value));
}

FloatInterval parse_float_interval(const std::string_view& value,
                                   const std::string_view delims) {
    const auto parts = split_string(value, delims);
    if (parts.size() == 2) {
        return FloatInterval(std::stof(std::string(parts[0])),
                             std::stof(std::string(parts[1])));
    }

    throw std::invalid_argument(
        fmt::format("Input value:'{}' does not have the right format: xx-xx.", value));
}

DoubleInterval parse_double_interval(const std::string_view& value,
                                     const std::string_view delims) {
    const auto parts = split_string(value, delims);
    if (parts.size() == 2) {
        return DoubleInterval(std::stod(std::string(parts[0])),
                              std::stod(std::string(parts[1])));
    }

    throw std::invalid_argument(
        fmt::format("Input value:'{}' does not have the right format: xx-xx.", value));
}

} // namespace hgps::core