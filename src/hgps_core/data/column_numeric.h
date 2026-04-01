#pragma once

#include "column_primitive.h"

namespace hgps::core {

class FloatDataTableColumn : public PrimitiveDataTableColumn<float> {
  public:
    using PrimitiveDataTableColumn<float>::PrimitiveDataTableColumn;

    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

class DoubleDataTableColumn : public PrimitiveDataTableColumn<double> {
  public:
    using PrimitiveDataTableColumn<double>::PrimitiveDataTableColumn;

    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

class IntegerDataTableColumn : public PrimitiveDataTableColumn<int> {
  public:
    using PrimitiveDataTableColumn<int>::PrimitiveDataTableColumn;

    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};
} // namespace hgps::core
