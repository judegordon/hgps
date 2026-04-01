#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "column.h"

namespace hgps::core {

class DataTable {
  public:
    using IteratorType = std::vector<std::unique_ptr<DataTableColumn>>::const_iterator;

    std::size_t num_columns() const noexcept;

    std::size_t num_rows() const noexcept;

    std::vector<std::string> names() const;

    void add(std::unique_ptr<DataTableColumn> column);

    const DataTableColumn &column(std::size_t index) const;

    const DataTableColumn &column(const std::string &name) const;

    std::optional<std::reference_wrapper<const DataTableColumn>>
    column_if_exists(const std::string &name) const;

    IteratorType cbegin() const noexcept { return columns_.cbegin(); }

    IteratorType cend() const noexcept { return columns_.cend(); }

    std::string to_string() const noexcept;

  private:
    std::unique_ptr<std::mutex> sync_mtx_{std::make_unique<std::mutex>()};
    std::vector<std::string> names_{};
    std::unordered_map<std::string, std::size_t> index_{};
    std::vector<std::unique_ptr<DataTableColumn>> columns_{};
    size_t rows_count_ = 0;
};

} // namespace hgps::core

std::ostream &operator<<(std::ostream &stream, const hgps::core::DataTable &table);
