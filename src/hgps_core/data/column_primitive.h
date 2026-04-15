#pragma once

#include "column.h"
#include "column_iterator.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace hgps::core {

template <typename TYPE>
class PrimitiveDataTableColumn : public DataTableColumn {
  public:
    using value_type = TYPE;
    using IteratorType = DataTableColumnIterator<PrimitiveDataTableColumn<TYPE>>;

    explicit PrimitiveDataTableColumn(std::string&& name, std::vector<TYPE>&& data)
        : DataTableColumn(), name_{std::move(name)}, data_{std::move(data)} {
        if (name_.length() < 2 ||
            !std::isalpha(static_cast<unsigned char>(name_.front()))) {
            throw std::invalid_argument(
                "Invalid column name: minimum length of two and start with alpha character.");
        }

        null_count_ = 0;
    }

    explicit PrimitiveDataTableColumn(std::string&& name, std::vector<TYPE>&& data,
                                      std::vector<bool>&& null_bitmap)
        : DataTableColumn(),
          name_{std::move(name)},
          data_{std::move(data)},
          null_bitmap_{std::move(null_bitmap)} {
        if (name_.length() < 2 ||
            !std::isalpha(static_cast<unsigned char>(name_.front()))) {
            throw std::invalid_argument(
                "Invalid column name: minimum length of two and start with alpha character.");
        }

        if (data_.size() != null_bitmap_.size()) {
            throw std::out_of_range(
                "Input vectors size mismatch, the data and valid vectors size must be the same.");
        }

        null_count_ = std::count(null_bitmap_.begin(), null_bitmap_.end(), false);
    }

    std::string type() const noexcept override { return typeid(TYPE).name(); }

    const std::string& name() const noexcept override { return name_; }

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

        return std::any();
    }

    std::optional<value_type> value_safe(std::size_t index) const noexcept {
        if (is_valid(index)) {
            return data_[index];
        }

        return std::nullopt;
    }

    value_type value_unsafe(std::size_t index) const { return data_[index]; }

    IteratorType begin() const { return IteratorType(*this); }

    IteratorType end() const { return IteratorType(*this, size()); }

  private:
    std::string name_;
    std::vector<TYPE> data_;
    std::vector<bool> null_bitmap_{};
    std::size_t null_count_ = 0;
};

class StringDataTableColumn : public PrimitiveDataTableColumn<std::string> {
  public:
    using PrimitiveDataTableColumn<std::string>::PrimitiveDataTableColumn;

    std::string type() const noexcept override { return "string"; }

    void accept(DataTableColumnVisitor& visitor) const override { visitor.visit(*this); }
};

} // namespace hgps::core