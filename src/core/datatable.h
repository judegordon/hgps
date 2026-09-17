// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/datatable.h, datatable.cpp.
#pragma once

#include "column_primitive.h"

#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace hgps::core {

/// @brief An in-memory table of named, typed columns.
///
/// Columns are looked up case-insensitively. Column names keep the order they were added in, so
/// anything that iterates the table is iterating a defined sequence.
///
/// Unlike the baseline's version this type carries no mutex: a DataTable is built by one loader
/// and read-only afterwards, and the lock only ever protected `add` against a concurrent `add`
/// that never happens.
class DataTable {
  public:
    using IteratorType = std::vector<std::unique_ptr<DataTableColumn>>::const_iterator;

    std::size_t num_columns() const noexcept;
    std::size_t num_rows() const noexcept;

    /// @brief The column names, in the order they were added.
    const std::vector<std::string> &names() const noexcept;

    /// @throws std::invalid_argument for a duplicate name or a size mismatch with existing columns.
    void add(std::unique_ptr<DataTableColumn> column);

    /// @throws std::out_of_range for an index outside the table.
    const DataTableColumn &column(std::size_t index) const;

    /// @throws std::out_of_range if no column has that name.
    const DataTableColumn &column(const std::string &name) const;

    std::optional<std::reference_wrapper<const DataTableColumn>>
    column_if_exists(const std::string &name) const;

    bool contains(const std::string &name) const;

    IteratorType cbegin() const noexcept { return columns_.cbegin(); }
    IteratorType cend() const noexcept { return columns_.cend(); }

    std::string to_string() const noexcept;

  private:
    std::vector<std::string> names_{};
    std::unordered_map<std::string, std::size_t> index_{};
    std::vector<std::unique_ptr<DataTableColumn>> columns_{};
    std::size_t rows_count_{0};
};

} // namespace hgps::core

std::ostream &operator<<(std::ostream &stream, const hgps::core::DataTable &table);
