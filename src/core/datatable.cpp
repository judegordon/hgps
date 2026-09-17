#include "datatable.h"

#include "string_util.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>

namespace hgps::core {

std::size_t DataTable::num_columns() const noexcept { return columns_.size(); }

std::size_t DataTable::num_rows() const noexcept { return rows_count_; }

const std::vector<std::string> &DataTable::names() const noexcept { return names_; }

void DataTable::add(std::unique_ptr<DataTableColumn> column) {
    if (!column) {
        throw std::invalid_argument("Cannot add a null column to a table.");
    }

    if (!columns_.empty() && column->size() != rows_count_) {
        throw std::invalid_argument(
            "Column size mismatch, new columns must have the same size of existing ones.");
    }

    auto column_key = to_lower(column->name());
    if (index_.contains(column_key)) {
        throw std::invalid_argument("Duplicated column name is not allowed.");
    }

    rows_count_ = column->size();
    names_.push_back(column->name());
    index_[std::move(column_key)] = columns_.size();
    columns_.push_back(std::move(column));
}

const DataTableColumn &DataTable::column(std::size_t index) const { return *columns_.at(index); }

const DataTableColumn &DataTable::column(const std::string &name) const {
    const auto found = index_.find(to_lower(name));
    if (found != index_.end()) {
        return *columns_.at(found->second);
    }

    throw std::out_of_range(fmt::format("Column name: {} not found.", name));
}

std::optional<std::reference_wrapper<const DataTableColumn>>
DataTable::column_if_exists(const std::string &name) const {
    const auto found = index_.find(to_lower(name));
    if (found != index_.end()) {
        return std::cref(*columns_.at(found->second));
    }
    return std::nullopt;
}

bool DataTable::contains(const std::string &name) const {
    return index_.contains(to_lower(name));
}

std::string DataTable::to_string() const noexcept {
    std::stringstream ss;

    std::size_t longest_name = 0;
    for (const auto &col : columns_) {
        longest_name = std::max(longest_name, col->name().length());
    }

    const auto pad = longest_name + 4;
    const auto width = pad + 28;

    ss << fmt::format("\n Table size: {} x {}\n", num_columns(), num_rows());
    ss << fmt::format("|{:-<{}}|\n", '-', width);
    ss << fmt::format("| {:{}} : {:10} : {:>10} |\n", "Column Name", pad, "Data Type", "# Nulls");
    ss << fmt::format("|{:-<{}}|\n", '-', width);
    for (const auto &col : columns_) {
        ss << fmt::format("| {:{}} : {:10} : {:10} |\n", col->name(), pad, col->type(),
                          col->null_count());
    }
    ss << fmt::format("|{:_<{}}|\n\n", '_', width);

    return ss.str();
}

} // namespace hgps::core

std::ostream &operator<<(std::ostream &stream, const hgps::core::DataTable &table) {
    stream << table.to_string();
    return stream;
}
