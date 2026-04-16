#include "univariate_visitor.h"

#include <stdexcept>
#include <variant>

namespace hgps {

namespace {

template <typename T>
core::UnivariateSummary summarise_numeric_column(const core::DataTableColumn& column,
                                                 const std::vector<T>& values) {
    core::UnivariateSummary summary(column.name);

    if (column.null_count() > 0) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (column.is_valid(i)) {
                summary.append(values[i]);
            } else {
                summary.append(std::nullopt);
            }
        }

        return summary;
    }

    for (const auto& value : values) {
        summary.append(value);
    }

    return summary;
}

} // namespace

core::UnivariateSummary UnivariateVisitor::get_summary(const core::DataTableColumn& column) const {
    return std::visit(
        [&](const auto& values) -> core::UnivariateSummary {
            using ValueType = typename std::decay_t<decltype(values)>::value_type;

            if constexpr (std::is_same_v<ValueType, std::string>) {
                throw std::invalid_argument(
                    "Statistical summary is not available for column type string: " + column.name);
            } else {
                return summarise_numeric_column(column, values);
            }
        },
        column.values);
}

} // namespace hgps