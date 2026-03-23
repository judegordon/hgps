#pragma once

#include <stdexcept>

#include "hgps_core/column_numeric.h"
#include "hgps_core/univariate_summary.h"
#include "hgps_core/visitor.h"

namespace hgps {

class UnivariateVisitor : public core::DataTableColumnVisitor {
  public:
    core::UnivariateSummary get_summary();

    void visit(const core::StringDataTableColumn &column) override;

    void visit(const core::FloatDataTableColumn &column) override;

    void visit(const core::DoubleDataTableColumn &column) override;

    void visit(const core::IntegerDataTableColumn &column) override;

  private:
    core::UnivariateSummary summary_;
};
} // namespace hgps
