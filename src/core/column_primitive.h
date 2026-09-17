// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/column_primitive.h, column_numeric.h, column_builder.h,
//         column_iterator.h.
#pragma once

#include "chars.h"
#include "column.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hgps::core {

/// @brief Iterates a column, yielding std::optional<T> so a null reads as an empty optional.
template <typename ColumnType> class DataTableColumnIterator {
  public:
    using value_type = std::optional<typename ColumnType::value_type>;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;
    using iterator_category = std::input_iterator_tag;

    DataTableColumnIterator() = default;

    explicit DataTableColumnIterator(const ColumnType &column, std::size_t index = 0)
        : column_{&column}, index_{index} {}

    std::size_t index() const noexcept { return index_; }

    value_type operator*() const { return column_->value_safe(index_); }

    DataTableColumnIterator &operator++() {
        ++index_;
        return *this;
    }

    DataTableColumnIterator operator++(int) {
        auto copy = *this;
        ++index_;
        return copy;
    }

    DataTableColumnIterator &operator--() {
        --index_;
        return *this;
    }

    bool operator==(const DataTableColumnIterator &rhs) const noexcept {
        return column_ == rhs.column_ && index_ == rhs.index_;
    }

  private:
    const ColumnType *column_{nullptr};
    std::size_t index_{0};
};

/// @brief A column of one primitive type.
///
/// A column with no nulls carries no null bitmap at all, which is the common case for the
/// simulation's input dataset.
template <typename TYPE> class PrimitiveDataTableColumn : public DataTableColumn {
  public:
    using value_type = TYPE;
    using IteratorType = DataTableColumnIterator<PrimitiveDataTableColumn<TYPE>>;

    /// @throws std::invalid_argument for a name shorter than two characters or not starting with
    ///         a letter.
    PrimitiveDataTableColumn(std::string name, std::vector<TYPE> data)
        : name_{std::move(name)}, data_{std::move(data)} {
        validate_name();
    }

    /// @throws std::invalid_argument for an invalid name.
    /// @throws std::out_of_range if the data and null bitmap sizes differ.
    PrimitiveDataTableColumn(std::string name, std::vector<TYPE> data,
                             std::vector<bool> null_bitmap)
        : name_{std::move(name)}, data_{std::move(data)}, null_bitmap_{std::move(null_bitmap)} {
        validate_name();

        if (data_.size() != null_bitmap_.size()) {
            throw std::out_of_range(
                "Input vectors size mismatch, the data and valid vectors size must be the same.");
        }

        null_count_ = static_cast<std::size_t>(
            std::count(null_bitmap_.begin(), null_bitmap_.end(), false));
    }

    std::string type() const noexcept override { return type_name(); }

    const std::string &name() const noexcept override { return name_; }

    std::size_t null_count() const noexcept override { return null_count_; }

    std::size_t size() const noexcept override { return data_.size(); }

    bool is_null(std::size_t index) const noexcept override {
        if (index >= size()) {
            return true;
        }
        return !null_bitmap_.empty() && !null_bitmap_[index];
    }

    bool is_valid(std::size_t index) const noexcept override {
        if (index >= size()) {
            return false;
        }
        return null_bitmap_.empty() || null_bitmap_[index];
    }

    std::any value(std::size_t index) const noexcept override {
        if (is_valid(index)) {
            return data_[index];
        }
        return std::any{};
    }

    /// @brief The value at an index, or nullopt for a null or out-of-range index.
    std::optional<value_type> value_safe(std::size_t index) const noexcept {
        if (is_valid(index)) {
            return data_[index];
        }
        return std::nullopt;
    }

    /// @warning No bounds check. Reading outside the column is undefined behaviour.
    const value_type &value_unsafe(std::size_t index) const { return data_[index]; }

    IteratorType begin() const { return IteratorType(*this); }
    IteratorType end() const { return IteratorType(*this, size()); }

    static std::string type_name();

  private:
    std::string name_;
    std::vector<TYPE> data_;
    std::vector<bool> null_bitmap_{};
    std::size_t null_count_{0};

    void validate_name() const {
        if (name_.length() < 2 || !chars::is_alpha(name_.front())) {
            throw std::invalid_argument(
                "Invalid column name: minimum length of two and start with alpha character.");
        }
    }
};

template <> inline std::string PrimitiveDataTableColumn<std::string>::type_name() {
    return "string";
}
template <> inline std::string PrimitiveDataTableColumn<float>::type_name() { return "float"; }
template <> inline std::string PrimitiveDataTableColumn<double>::type_name() { return "double"; }
template <> inline std::string PrimitiveDataTableColumn<int>::type_name() { return "integer"; }

class StringDataTableColumn final : public PrimitiveDataTableColumn<std::string> {
  public:
    using PrimitiveDataTableColumn<std::string>::PrimitiveDataTableColumn;
    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

class FloatDataTableColumn final : public PrimitiveDataTableColumn<float> {
  public:
    using PrimitiveDataTableColumn<float>::PrimitiveDataTableColumn;
    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

class DoubleDataTableColumn final : public PrimitiveDataTableColumn<double> {
  public:
    using PrimitiveDataTableColumn<double>::PrimitiveDataTableColumn;
    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

class IntegerDataTableColumn final : public PrimitiveDataTableColumn<int> {
  public:
    using PrimitiveDataTableColumn<int>::PrimitiveDataTableColumn;
    void accept(DataTableColumnVisitor &visitor) const override { visitor.visit(*this); }
};

/// @brief Accumulates values and nulls, then builds a column once.
template <typename ColumnType> class PrimitiveDataTableColumnBuilder {
  public:
    using value_type = typename ColumnType::value_type;

    PrimitiveDataTableColumnBuilder() = delete;

    /// @throws std::invalid_argument for an invalid column name.
    explicit PrimitiveDataTableColumnBuilder(std::string name) : name_{std::move(name)} {
        if (name_.length() < 2 || !chars::is_alpha(name_.front())) {
            throw std::invalid_argument(
                "Invalid column name: minimum length of two and start with alpha character.");
        }
    }

    const std::string &name() const noexcept { return name_; }
    std::size_t size() const noexcept { return data_.size(); }
    std::size_t null_count() const noexcept { return null_count_; }
    std::size_t capacity() const noexcept { return data_.capacity(); }

    void reserve(std::size_t capacity) {
        data_.reserve(capacity);
        null_bitmap_.reserve(capacity);
    }

    void append_null() { append_null_internal(1); }
    void append_null(std::size_t count) { append_null_internal(count); }

    void append(value_type value) {
        data_.push_back(std::move(value));
        null_bitmap_.push_back(true);
    }

    const value_type &value(std::size_t index) const { return data_[index]; }
    const value_type &operator[](std::size_t index) const { return value(index); }
    value_type &operator[](std::size_t index) { return data_[index]; }

    void reset() {
        data_.clear();
        null_bitmap_.clear();
        null_count_ = 0;
    }

    [[nodiscard]] std::unique_ptr<ColumnType> build() {
        data_.shrink_to_fit();
        null_bitmap_.shrink_to_fit();

        if (null_count_ > 0) {
            return std::make_unique<ColumnType>(std::move(name_), std::move(data_),
                                                std::move(null_bitmap_));
        }

        return std::make_unique<ColumnType>(std::move(name_), std::move(data_));
    }

  private:
    std::string name_;
    std::vector<value_type> data_{};
    std::vector<bool> null_bitmap_{};
    std::size_t null_count_{0};

    void append_null_internal(std::size_t length) {
        null_count_ += length;
        data_.insert(data_.end(), length, value_type{});
        null_bitmap_.insert(null_bitmap_.end(), length, false);
    }
};

using StringDataTableColumnBuilder = PrimitiveDataTableColumnBuilder<StringDataTableColumn>;
using FloatDataTableColumnBuilder = PrimitiveDataTableColumnBuilder<FloatDataTableColumn>;
using DoubleDataTableColumnBuilder = PrimitiveDataTableColumnBuilder<DoubleDataTableColumn>;
using IntegerDataTableColumnBuilder = PrimitiveDataTableColumnBuilder<IntegerDataTableColumn>;

} // namespace hgps::core
