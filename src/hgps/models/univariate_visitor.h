#pragma once

#include "hgps_core/data/column.h"
#include "hgps_core/types/univariate_summary.h"

namespace hgps {

class UnivariateVisitor {
  public:
    core::UnivariateSummary get_summary(const core::DataTableColumn &column) const;
};

} // namespace hgps