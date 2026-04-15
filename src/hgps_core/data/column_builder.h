#pragma once

#include "column.h"

#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>

namespace hgps::core {

template <typename ColumnType>
class PrimitiveDataTableColumnBuilder {
  public:
    using value_type = typename ColumnType::value_type;

    PrimitiveDataTableColumnBuilder() = delete;

    explicit PrimitiveDataTableColumnBuilder(std::string name) : name_{std::move(name)} {
        if (name_.length() < 2 ||
            !std::isalpha(static_cast<unsigned char>(name_.front()))) {
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
        if (null_bitmap_.has_value()) {
            null_bitmap_->reserve(capacity);
        }
    }

    void append_null() { append_null_internal(1); }

    void append_null(std::size_t count) { append_null_internal(count); }

    void append(value_type value) {
        data_.push_back(value);
        if (null_bitmap_.has_value()) {
            null_bitmap_->push_back(true);
        }
    }

    value_type value(std::size_t index) const { return data_[index]; }

    value_type operator[](std::size_t index) const { return value(index); }

    value_type &operator[](std::size_t index) { return data_[index]; }

    void reset() {
        data_.clear();
        null_bitmap_.reset();
        null_count_ = 0;
    }

    [[nodiscard]] std::unique_ptr<ColumnType> build() {
        data_.shrink_to_fit();

        if (null_count_ > 0) {
            null_bitmap_->shrink_to_fit();
            return std::make_unique<ColumnType>(std::move(name_), std::move(data_),
                                                std::move(*null_bitmap_));
        }

        return std::make_unique<ColumnType>(std::move(name_), std::move(data_));
    }

  private:
    std::string name_;
    std::vector<value_type> data_{};
    std::optional<std::vector<bool>> null_bitmap_{};
    std::size_t null_count_ = 0;

    void ensure_null_bitmap() {
        if (!null_bitmap_.has_value()) {
            null_bitmap_.emplace(data_.size(), true);
        }
    }

    void append_null_internal(std::size_t length) {
        ensure_null_bitmap();
        null_count_ += length;
        data_.insert(data_.end(), length, value_type{});
        null_bitmap_->insert(null_bitmap_->end(), length, false);
    }
};

using StringDataTableColumnBuilder = PrimitiveDataTableColumnBuilder<StringDataTableColumn>;
using FloatDataTableColumnBuilder = PrimitiveDataTableColumnBuilder<FloatDataTableColumn>;
using DoubleDataTableColumnBuilder = PrimitiveDataTableColumnBuilder<DoubleDataTableColumn>;
using IntegerDataTableColumnBuilder = PrimitiveDataTableColumnBuilder<IntegerDataTableColumn>;

} // namespace hgps::core