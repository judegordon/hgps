#include "data_table.h"
#include "utils/string_util.h"

#include <fmt/format.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace hgps::core {

namespace {

const char* column_type_to_string(DataTableColumnType type) noexcept {
    switch (type) {
    case DataTableColumnType::string:
        return "string";
    case DataTableColumnType::float32:
        return "float32";
    case DataTableColumnType::float64:
        return "float64";
    case DataTableColumnType::integer:
        return "integer";
    }
    return "unknown";
}

} // namespace

std::size_t DataTable::num_columns() const noexcept { return columns_.size(); }

std::size_t DataTable::num_rows() const noexcept { return rows_count_; }

std::vector<std::string> DataTable::names() const {
    std::vector<std::string> result;
    result.reserve(columns_.size());
    for (const auto &col : columns_) {
        result.push_back(col.name);
    }
    return result;
}

void DataTable::add(DataTableColumn column) {
    if (num_columns() > 0 && column.size() != rows_count_) {
        throw std::invalid_argument(
            "Column size mismatch, new columns must have the same size as existing ones.");
    }

    auto column_key = to_lower(column.name);
    if (index_.contains(column_key)) {
        throw std::invalid_argument("Duplicated column name is not allowed.");
    }

    rows_count_ = column.size();
    index_[column_key] = columns_.size();
    columns_.push_back(std::move(column));
}

const DataTableColumn &DataTable::column(std::size_t index) const {
    return columns_.at(index);
}

const DataTableColumn &DataTable::column(const std::string &name) const {
    auto found = index_.find(to_lower(name));
    if (found != index_.end()) {
        return columns_.at(found->second);
    }

    throw std::out_of_range(fmt::format("Column name: {} not found.", name));
}

std::optional<std::reference_wrapper<const DataTableColumn>>
DataTable::column_if_exists(const std::string &name) const {
    auto found = index_.find(to_lower(name));
    if (found != index_.end()) {
        return std::cref(columns_.at(found->second));
    }
    return std::nullopt;
}

std::string DataTable::to_string() const {
    std::ostringstream ss;
    std::size_t longest_column_name = 0;
    for (const auto &col : columns_) {
        longest_column_name = std::max(longest_column_name, col.name.length());
    }

    const auto pad = longest_column_name + 4;
    const auto width = pad + 28;

    ss << fmt::format("\n Table size: {} x {}\n", num_columns(), num_rows());
    ss << fmt::format("|{:-<{}}|\n", '-', width);
    ss << fmt::format("| {:{}} : {:10} : {:>10} |\n", "Column Name", pad, "Data Type", "# Nulls");
    ss << fmt::format("|{:-<{}}|\n", '-', width);

    for (const auto &col : columns_) {
        ss << fmt::format("| {:{}} : {:10} : {:10} |\n",
                          col.name,
                          pad,
                          column_type_to_string(col.type()),
                          col.null_count());
    }

    ss << fmt::format("|{:_<{}}|\n\n", '_', width);
    return ss.str();
}

} // namespace hgps::core

std::ostream &operator<<(std::ostream &stream, const hgps::core::DataTable &table) {
    stream << table.to_string();
    return stream;
}