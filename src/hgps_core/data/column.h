#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace hgps::core {

enum class DataTableColumnType {
    string,
    float32,
    float64,
    integer,
};

using DataTableColumnValues =
    std::variant<std::vector<std::string>, std::vector<float>, std::vector<double>,
                 std::vector<int>>;

struct DataTableColumn {
    std::string name;
    DataTableColumnValues values;
    std::vector<bool> valid{};

    DataTableColumn() = default;

    DataTableColumn(std::string name_in, DataTableColumnValues values_in,
                    std::vector<bool> valid_in = {})
        : name(std::move(name_in)), values(std::move(values_in)), valid(std::move(valid_in)) {
        if (name.length() < 2 ||
            !std::isalpha(static_cast<unsigned char>(name.front()))) {
            throw std::invalid_argument(
                "Invalid column name: minimum length of two and start with alpha character.");
        }

        if (!valid.empty() && valid.size() != size()) {
            throw std::invalid_argument(
                "Input vectors size mismatch, data and valid bitmap must be the same size.");
        }
    }

    DataTableColumnType type() const noexcept {
        return std::visit(
            []<typename T>(const T&) -> DataTableColumnType {
                using ValueType = typename T::value_type;

                if constexpr (std::is_same_v<ValueType, std::string>) {
                    return DataTableColumnType::string;
                } else if constexpr (std::is_same_v<ValueType, float>) {
                    return DataTableColumnType::float32;
                } else if constexpr (std::is_same_v<ValueType, double>) {
                    return DataTableColumnType::float64;
                } else {
                    return DataTableColumnType::integer;
                }
            },
            values);
    }

    std::size_t size() const noexcept {
        return std::visit([](const auto& v) { return v.size(); }, values);
    }

    bool is_valid(std::size_t index) const noexcept {
        if (index >= size()) {
            return false;
        }

        return valid.empty() || valid[index];
    }

    bool is_null(std::size_t index) const noexcept { return !is_valid(index); }

    std::size_t null_count() const noexcept {
        if (valid.empty()) {
            return 0;
        }

        return static_cast<std::size_t>(std::count(valid.begin(), valid.end(), false));
    }
};

} // namespace hgps::core