// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{monotonic_vector,gender_table,map2d,gender_value}.h.
#pragma once

#include "core/array2d.h"
#include "core/interval.h"
#include "core/types.h"

#include <map>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hgps::model {

/// @brief A pair of values, one per sex.
template <core::Numerical TYPE> struct GenderValue {
    TYPE male{};
    TYPE female{};

    TYPE &at(core::Gender gender) { return gender == core::Gender::male ? male : female; }
    const TYPE &at(core::Gender gender) const {
        return gender == core::Gender::male ? male : female;
    }

    auto operator<=>(const GenderValue &) const = default;
    bool operator==(const GenderValue &) const = default;
};

/// @brief True if the values are strictly increasing or strictly decreasing.
template <core::Numerical TYPE> bool is_strict_monotonic(const std::vector<TYPE> &values) noexcept {
    if (values.size() < 2) {
        return true;
    }

    const bool increasing = values[1] > values[0];
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] == values[i - 1]) {
            return false;
        }
        if ((values[i] > values[i - 1]) != increasing) {
            return false;
        }
    }

    return true;
}

/// @brief A vector whose values are known to be strictly monotonic — the breakpoints of a lookup
///        table, where a repeated or out-of-order value would silently shadow a row.
template <core::Numerical TYPE> class MonotonicVector {
  public:
    using IteratorType = typename std::vector<TYPE>::iterator;
    using ConstIteratorType = typename std::vector<TYPE>::const_iterator;

    /// @throws std::invalid_argument if the values are not strictly monotonic.
    explicit MonotonicVector(std::vector<TYPE> values) : data_{std::move(values)} {
        if (!is_strict_monotonic(data_)) {
            throw std::invalid_argument("Values must be strict monotonic.");
        }
    }

    std::size_t size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.empty(); }

    /// @throws std::out_of_range outside the vector.
    const TYPE &at(std::size_t index) const { return data_.at(index); }
    const TYPE &operator[](std::size_t index) const { return data_.at(index); }

    ConstIteratorType begin() const noexcept { return data_.cbegin(); }
    ConstIteratorType end() const noexcept { return data_.cend(); }
    ConstIteratorType cbegin() const noexcept { return data_.cbegin(); }
    ConstIteratorType cend() const noexcept { return data_.cend(); }

  private:
    std::vector<TYPE> data_;
};

/// @brief A lookup table indexed by a monotonic row key and a sex.
template <core::Numerical ROW, core::Numerical TYPE> class GenderTable {
  public:
    GenderTable() = default;

    /// @throws std::invalid_argument if the breakpoints and the value table disagree in size.
    GenderTable(const MonotonicVector<ROW> &rows, const std::vector<core::Gender> &columns,
                core::Array2D<TYPE> values)
        : table_{std::move(values)} {
        if (rows.size() != table_.rows() || columns.size() != table_.columns()) {
            throw std::invalid_argument("Lookup breakpoints and values size mismatch.");
        }

        for (std::size_t index = 0; index < rows.size(); ++index) {
            rows_index_.emplace(rows[index], index);
        }
        for (std::size_t index = 0; index < columns.size(); ++index) {
            columns_index_.emplace(columns[index], index);
        }
    }

    std::size_t size() const noexcept { return table_.size(); }
    std::size_t rows() const noexcept { return table_.rows(); }
    std::size_t columns() const noexcept { return table_.columns(); }
    bool empty() const noexcept { return rows_index_.empty() || columns_index_.empty(); }

    /// @throws std::out_of_range for an unknown breakpoint.
    TYPE &at(ROW row, core::Gender gender) {
        return table_(rows_index_.at(row), columns_index_.at(gender));
    }
    const TYPE &at(ROW row, core::Gender gender) const {
        return table_(rows_index_.at(row), columns_index_.at(gender));
    }

    TYPE &operator()(ROW row, core::Gender gender) { return at(row, gender); }
    const TYPE &operator()(ROW row, core::Gender gender) const { return at(row, gender); }

    bool contains(ROW row) const noexcept { return rows_index_.contains(row); }

    bool contains(ROW row, core::Gender gender) const noexcept {
        return rows_index_.contains(row) && columns_index_.contains(gender);
    }

    /// @brief The row breakpoints, in order. Iterating a table is iterating this.
    std::vector<ROW> row_keys() const {
        std::vector<ROW> keys;
        keys.reserve(rows_index_.size());
        for (const auto &[key, index] : rows_index_) {
            keys.push_back(key);
        }
        return keys;
    }

  private:
    core::Array2D<TYPE> table_{};

    // std::map, so row_keys() is ordered and anything iterating the table is deterministic.
    std::map<ROW, std::size_t> rows_index_{};
    std::map<core::Gender, std::size_t> columns_index_{};
};

/// @brief A lookup table indexed by age and sex.
template <core::Numerical TYPE> class AgeGenderTable : public GenderTable<int, TYPE> {
  public:
    AgeGenderTable() = default;

    AgeGenderTable(const MonotonicVector<int> &rows, const std::vector<core::Gender> &columns,
                   core::Array2D<TYPE> values)
        : GenderTable<int, TYPE>(rows, columns, std::move(values)) {}
};

/// @brief A zero-filled gender table over an integer range.
/// @throws std::out_of_range for a negative lower bound or a lower bound not below the upper.
template <core::Numerical TYPE>
GenderTable<int, TYPE> create_integer_gender_table(const core::IntegerInterval &rows_range) {
    if (rows_range.lower() < 0 || rows_range.lower() >= rows_range.upper()) {
        throw std::out_of_range("The 'range lower' value must be greater than zero and less than "
                                "the 'range upper' value.");
    }

    std::vector<int> rows(static_cast<std::size_t>(rows_range.length()) + 1);
    std::iota(rows.begin(), rows.end(), rows_range.lower());

    const std::vector<core::Gender> columns{core::Gender::male, core::Gender::female};
    return GenderTable<int, TYPE>(MonotonicVector<int>{rows}, columns,
                                  core::Array2D<TYPE>(rows.size(), columns.size()));
}

/// @brief A zero-filled age and gender table over an age range.
/// @throws std::invalid_argument for a negative lower bound or an empty range.
template <core::Numerical TYPE>
AgeGenderTable<TYPE> create_age_gender_table(const core::IntegerInterval &age_range) {
    if (age_range.lower() < 0 || age_range.lower() == age_range.upper()) {
        throw std::invalid_argument(
            "The 'age lower' value must be greater than zero and less than the 'age upper' value.");
    }

    std::vector<int> rows(static_cast<std::size_t>(age_range.length()) + 1);
    std::iota(rows.begin(), rows.end(), age_range.lower());

    const std::vector<core::Gender> columns{core::Gender::male, core::Gender::female};
    return AgeGenderTable<TYPE>(MonotonicVector<int>{rows}, columns,
                                core::Array2D<TYPE>(rows.size(), columns.size()));
}

using IntegerAgeGenderTable = AgeGenderTable<int>;
using FloatAgeGenderTable = AgeGenderTable<float>;
using DoubleAgeGenderTable = AgeGenderTable<double>;

/// @brief A two-dimensional lookup keyed by row then column.
///
/// Always an ordered map underneath, unlike the baseline, which offers an unordered variant as
/// well. Anything that iterates one of these is producing a result, and the order of that result
/// must not come from a hash table (determinism clause D4, audit B-05).
template <class TRow, class TCol, class TCell> class Map2d {
  public:
    using RowType = std::map<TCol, TCell>;
    using TableType = std::map<TRow, RowType>;
    using IteratorType = typename TableType::iterator;
    using ConstIteratorType = typename TableType::const_iterator;

    Map2d() = default;
    explicit Map2d(TableType data) noexcept : table_{std::move(data)} {}

    IteratorType begin() noexcept { return table_.begin(); }
    IteratorType end() noexcept { return table_.end(); }
    ConstIteratorType begin() const noexcept { return table_.cbegin(); }
    ConstIteratorType end() const noexcept { return table_.cend(); }
    ConstIteratorType cbegin() const noexcept { return table_.cbegin(); }
    ConstIteratorType cend() const noexcept { return table_.cend(); }

    bool empty() const noexcept { return table_.empty(); }

    /// @throws std::out_of_range for an unknown row.
    bool empty(const TRow &row) const { return table_.at(row).empty(); }

    std::size_t rows() const noexcept { return table_.size(); }

    /// @throws std::out_of_range for an unknown row.
    std::size_t columns(const TRow &row) const { return table_.at(row).size(); }

    bool contains(const TRow &row) const noexcept { return table_.contains(row); }

    bool contains(const TRow &row, const TCol &column) const noexcept {
        const auto found = table_.find(row);
        return found != table_.end() && found->second.contains(column);
    }

    /// @throws std::out_of_range for an unknown row or column.
    RowType &at(const TRow &row) { return table_.at(row); }
    const RowType &at(const TRow &row) const { return table_.at(row); }
    TCell &at(const TRow &row, const TCol &column) { return table_.at(row).at(column); }
    const TCell &at(const TRow &row, const TCol &column) const {
        return table_.at(row).at(column);
    }

    /// @brief Inserts or replaces a cell, creating the row if needed.
    void emplace(const TRow &row, const TCol &column, TCell value) {
        table_[row][column] = std::move(value);
    }

    void emplace_row(const TRow &row, RowType values) { table_[row] = std::move(values); }

  private:
    TableType table_;
};

template <class TRow, class TCol, class TCell> using OrderedMap2d = Map2d<TRow, TCol, TCell>;

} // namespace hgps::model
