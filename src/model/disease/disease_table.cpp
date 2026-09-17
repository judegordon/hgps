#include "disease_table.h"

#include <cmath>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {

DiseaseTable::DiseaseTable(core::DiseaseInfo info, std::map<std::string, int> measures,
                           std::map<int, std::map<core::Gender, DiseaseMeasure>> data)
    : info_{std::move(info)}, measures_{std::move(measures)}, data_{std::move(data)} {
    if (info_.code.is_empty()) {
        throw std::invalid_argument("Invalid disease information with empty identifier");
    }

    if (data_.empty()) {
        return;
    }

    const auto column_count = data_.begin()->second.size();
    for (const auto &[age, by_gender] : data_) {
        if (by_gender.size() != column_count) {
            throw std::invalid_argument(
                fmt::format("Number of columns mismatch at age: {} ({} vs {}).", age,
                            by_gender.size(), column_count));
        }
    }
}

std::size_t DiseaseTable::columns() const noexcept {
    return data_.empty() ? 0 : data_.begin()->second.size();
}

RelativeRiskLookup::RelativeRiskLookup(const MonotonicVector<int> &rows,
                                       const MonotonicVector<float> &columns,
                                       core::FloatArray2D values)
    : table_{std::move(values)} {
    if (rows.size() != table_.rows() || columns.size() != table_.columns()) {
        throw std::out_of_range("Lookup breakpoints and values size mismatch.");
    }

    for (std::size_t index = 0; index < rows.size(); ++index) {
        rows_index_.emplace(rows[index], index);
    }
    for (std::size_t index = 0; index < columns.size(); ++index) {
        columns_index_.emplace(columns[index], index);
    }
}

float RelativeRiskLookup::lookup(int age, float value) const {
    if (empty()) {
        return std::nanf("");
    }

    const auto row = rows_index_.at(age);

    if (value <= columns_index_.begin()->first) {
        return table_(row, columns_index_.begin()->second);
    }
    if (value >= columns_index_.rbegin()->first) {
        return table_(row, columns_index_.rbegin()->second);
    }
    if (const auto exact = columns_index_.find(value); exact != columns_index_.end()) {
        return table_(row, exact->second);
    }

    const auto upper = columns_index_.lower_bound(value);
    const auto lower = std::prev(upper);

    const auto x1 = lower->first;
    const auto x2 = upper->first;
    const auto y1 = table_(row, lower->second);
    const auto y2 = table_(row, upper->second);

    return (y2 - y1) * (value - x1) / (x2 - x1) + y1;
}

DiseaseParameter::DiseaseParameter(int data_time, ParameterLookup prevalence,
                                   ParameterLookup survival, ParameterLookup deaths)
    : time_year{data_time}, prevalence_distribution{std::move(prevalence)},
      survival_rate{std::move(survival)}, death_weight{std::move(deaths)} {
    max_time_since_onset =
        prevalence_distribution.empty() ? 0 : prevalence_distribution.rbegin()->first + 1;
}

DiseaseDefinition::DiseaseDefinition(DiseaseTable measures, RelativeRiskTableMap diseases,
                                     RelativeRiskLookupMap risk_factors,
                                     DiseaseParameter parameter)
    : measures_{std::move(measures)}, relative_risk_diseases_{std::move(diseases)},
      relative_risk_factors_{std::move(risk_factors)}, parameters_{std::move(parameter)} {}

} // namespace hgps::model
