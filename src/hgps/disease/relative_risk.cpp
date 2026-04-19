#include "relative_risk.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace hgps {

RelativeRiskLookup::RelativeRiskLookup(const MonotonicVector<int> &rows,
                                       const MonotonicVector<float> &cols,
                                       core::FloatArray2D &&values)
    : table_{std::move(values)} {
    if (rows.empty() || cols.empty() || table_.empty()) {
        throw std::invalid_argument("Relative risk lookup must not be empty.");
    }

    if (rows.size() != table_.rows() || cols.size() != table_.columns()) {
        throw std::out_of_range("Lookup breakpoints and values size mismatch.");
    }

    const auto rows_count = static_cast<int>(rows.size());
    for (auto index = 0; index < rows_count; ++index) {
        rows_index_.emplace(rows[index], index);
    }

    const auto cols_count = static_cast<int>(cols.size());
    for (auto index = 0; index < cols_count; ++index) {
        cols_index_.emplace(cols[index], index);
    }
}

std::size_t RelativeRiskLookup::size() const noexcept { return table_.size(); }

std::size_t RelativeRiskLookup::rows() const noexcept { return table_.rows(); }

std::size_t RelativeRiskLookup::columns() const noexcept { return table_.columns(); }

bool RelativeRiskLookup::empty() const noexcept { return table_.empty(); }

float RelativeRiskLookup::at(const int age, const float value) const { return lookup_value(age, value); }

float RelativeRiskLookup::operator()(const int age, const float value) const {
    return lookup_value(age, value);
}

bool RelativeRiskLookup::contains(const int age, const float value) const noexcept {
    if (empty() || !rows_index_.contains(age)) {
        return false;
    }

    return value >= cols_index_.begin()->first && value <= cols_index_.rbegin()->first;
}

float RelativeRiskLookup::lookup_value(const int age, const float value) const {
    const auto row_index = rows_index_.at(age);

    if (value <= cols_index_.begin()->first) {
        return table_(row_index, cols_index_.begin()->second);
    }

    if (value >= cols_index_.rbegin()->first) {
        return table_(row_index, cols_index_.rbegin()->second);
    }

    if (cols_index_.contains(value)) {
        return table_(row_index, cols_index_.at(value));
    }

    const auto it = cols_index_.lower_bound(value);
    const auto it_prev = std::prev(it);

    const auto x1 = it_prev->first;
    const auto x2 = it->first;
    const auto y1 = table_(row_index, it_prev->second);
    const auto y2 = table_(row_index, it->second);

    return (y2 - y1) * (value - x1) / (x2 - x1) + y1;
}

} // namespace hgps