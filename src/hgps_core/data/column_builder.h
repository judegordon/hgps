#pragma once

#include "column.h"

#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace hgps::core {

template <typename T>
class DataTableColumnBuilder {
  public:
    using value_type = T;

    DataTableColumnBuilder() = delete;

    explicit DataTableColumnBuilder(std::string name) : name_{std::move(name)} {
        if (name_.length() < 2 ||
            !std::isalpha(static_cast<unsigned char>(name_.front()))) {
            throw std::invalid_argument(
                "Invalid column name: minimum length of two and start with alpha character.");
        }
    }

    const std::string& name() const noexcept { return name_; }

    std::size_t size() const noexcept { return data_.size(); }

    std::size_t null_count() const noexcept { return null_count_; }

    std::size_t capacity() const noexcept { return data_.capacity(); }

    void reserve(std::size_t capacity) {
        data_.reserve(capacity);
        if (valid_.has_value()) {
            valid_->reserve(capacity);
        }
    }

    void append(const T& value) {
        data_.push_back(value);
        if (valid_.has_value()) {
            valid_->push_back(true);
        }
    }

    void append(T&& value) {
        data_.push_back(std::move(value));
        if (valid_.has_value()) {
            valid_->push_back(true);
        }
    }

    void append_null() { append_null(1); }

    void append_null(std::size_t count) {
        ensure_valid_bitmap();
        null_count_ += count;
        data_.insert(data_.end(), count, T{});
        valid_->insert(valid_->end(), count, false);
    }

    const T& value(std::size_t index) const { return data_[index]; }

    const T& operator[](std::size_t index) const { return data_[index]; }

    T& operator[](std::size_t index) { return data_[index]; }

    void reset() {
        data_.clear();
        valid_.reset();
        null_count_ = 0;
    }

    [[nodiscard]] DataTableColumn build() {
        if (valid_.has_value() && valid_->size() != data_.size()) {
            throw std::logic_error("Invalid builder state: data and valid bitmap size mismatch.");
        }

        return DataTableColumn{
            .name = std::move(name_),
            .values = std::move(data_),
            .valid = valid_.has_value() ? std::move(*valid_) : std::vector<bool>{},
        };
    }

  private:
    std::string name_;
    std::vector<T> data_{};
    std::optional<std::vector<bool>> valid_{};
    std::size_t null_count_ = 0;

    void ensure_valid_bitmap() {
        if (!valid_.has_value()) {
            valid_.emplace(data_.size(), true);
        }
    }
};

} // namespace hgps::core