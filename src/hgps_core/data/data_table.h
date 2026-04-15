#pragma once

#include "column.h"

#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace hgps::core {

class DataTable {
  public:
    using IteratorType = std::vector<DataTableColumn>::const_iterator;

    std::size_t num_columns() const noexcept;
    std::size_t num_rows() const noexcept;

    std::vector<std::string> names() const;

    void add(DataTableColumn column);

    const DataTableColumn& column(std::size_t index) const;
    const DataTableColumn& column(const std::string& name) const;

    std::optional<std::reference_wrapper<const DataTableColumn>>
    column_if_exists(const std::string& name) const;

    IteratorType cbegin() const noexcept { return columns_.cbegin(); }
    IteratorType cend() const noexcept { return columns_.cend(); }

    std::string to_string() const;

  private:
    std::unordered_map<std::string, std::size_t> index_{};
    std::vector<DataTableColumn> columns_{};
    std::size_t rows_count_ = 0;
};

} // namespace hgps::core

std::ostream& operator<<(std::ostream& stream, const hgps::core::DataTable& table);