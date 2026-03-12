#pragma once
#include <any>
#include <string>
#include <typeinfo>

#include "visitor.h"

namespace hgps::core {

class DataTableColumn {
  public:
    DataTableColumn() = default;

    DataTableColumn(const DataTableColumn &) = delete;
    DataTableColumn &operator=(const DataTableColumn &) = delete;

    DataTableColumn(DataTableColumn &&) = delete;
    DataTableColumn &operator=(DataTableColumn &&) = delete;

    virtual ~DataTableColumn() = default;

    virtual std::string type() const noexcept = 0;

    virtual std::string name() const noexcept = 0;

    virtual std::size_t null_count() const noexcept = 0;

    virtual std::size_t size() const noexcept = 0;

    virtual bool is_null(std::size_t index) const noexcept = 0;

    virtual bool is_valid(std::size_t index) const noexcept = 0;

    virtual const std::any value(std::size_t index) const noexcept = 0;

    virtual void accept(DataTableColumnVisitor &visitor) const = 0;
};
} // namespace hgps::core
