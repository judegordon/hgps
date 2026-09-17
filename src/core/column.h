// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/column.h, visitor.h.
#pragma once

#include "types.h"

#include <any>
#include <cstddef>
#include <string>

namespace hgps::core {

class StringDataTableColumn;
class FloatDataTableColumn;
class DoubleDataTableColumn;
class IntegerDataTableColumn;

/// @brief Double-dispatch over the concrete column types.
class DataTableColumnVisitor {
  public:
    DataTableColumnVisitor() = default;
    DataTableColumnVisitor(const DataTableColumnVisitor &) = delete;
    DataTableColumnVisitor &operator=(const DataTableColumnVisitor &) = delete;
    DataTableColumnVisitor(DataTableColumnVisitor &&) = delete;
    DataTableColumnVisitor &operator=(DataTableColumnVisitor &&) = delete;
    virtual ~DataTableColumnVisitor() = default;

    virtual void visit(const StringDataTableColumn &column) = 0;
    virtual void visit(const FloatDataTableColumn &column) = 0;
    virtual void visit(const DoubleDataTableColumn &column) = 0;
    virtual void visit(const IntegerDataTableColumn &column) = 0;
};

/// @brief One column of a DataTable: a name, a type, values and a null mask.
class DataTableColumn {
  public:
    DataTableColumn() = default;
    DataTableColumn(const DataTableColumn &) = delete;
    DataTableColumn &operator=(const DataTableColumn &) = delete;
    DataTableColumn(DataTableColumn &&) = delete;
    DataTableColumn &operator=(DataTableColumn &&) = delete;
    virtual ~DataTableColumn() = default;

    virtual std::string type() const noexcept = 0;
    virtual const std::string &name() const noexcept = 0;
    virtual std::size_t null_count() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual bool is_null(std::size_t index) const noexcept = 0;
    virtual bool is_valid(std::size_t index) const noexcept = 0;

    /// @brief The value at an index, type-erased; empty for a null or out-of-range index.
    virtual std::any value(std::size_t index) const noexcept = 0;

    virtual void accept(DataTableColumnVisitor &visitor) const = 0;
};

} // namespace hgps::core
