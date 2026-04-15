#pragma once

#include "column_primitive.h"

namespace hgps::core {

class FloatDataTableColumn : public PrimitiveDataTableColumn<float> {
  public:
    using PrimitiveDataTableColumn<float>::PrimitiveDataTableColumn;

    std::string type() const noexcept override { return "float"; }

    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

class DoubleDataTableColumn : public PrimitiveDataTableColumn<double> {
  public:
    using PrimitiveDataTableColumn<double>::PrimitiveDataTableColumn;

    std::string type() const noexcept override { return "double"; }

    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

class IntegerDataTableColumn : public PrimitiveDataTableColumn<int> {
  public:
    using PrimitiveDataTableColumn<int>::PrimitiveDataTableColumn;

    std::string type() const noexcept override { return "int"; }

    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

} // namespace hgps::core