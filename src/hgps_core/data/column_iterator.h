#pragma once

#include <cstddef>
#include <iterator>
#include <stdexcept>

namespace hgps::core {

template <typename ColumnType>
class DataTableColumnIterator {
  public:
    using value_type = typename ColumnType::value_type;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using iterator_category = std::random_access_iterator_tag;

    DataTableColumnIterator() : column_(nullptr), index_(0) {}

    explicit DataTableColumnIterator(const ColumnType &column, std::size_t index = 0)
        : column_(&column), index_(index) {}

    std::size_t index() const noexcept { return index_; }

    value_type operator*() const {
        return column_->is_null(index_) ? value_type{} : column_->value_safe(index_).value();
    }

    DataTableColumnIterator &operator--() {
        --index_;
        return *this;
    }

    DataTableColumnIterator &operator++() {
        ++index_;
        return *this;
    }

    DataTableColumnIterator operator++(int) {
        DataTableColumnIterator tmp(*this);
        ++(*this);
        return tmp;
    }

    DataTableColumnIterator operator--(int) {
        DataTableColumnIterator tmp(*this);
        --(*this);
        return tmp;
    }

    difference_type operator-(const DataTableColumnIterator &other) const {
        if (column_ != other.column_) {
            throw std::logic_error("Cannot subtract iterators from different columns.");
        }

        return static_cast<difference_type>(index_) - static_cast<difference_type>(other.index_);
    }

    DataTableColumnIterator operator+(difference_type n) const {
        return DataTableColumnIterator(
            *column_, static_cast<std::size_t>(static_cast<difference_type>(index_) + n));
    }

    DataTableColumnIterator operator-(difference_type n) const {
        return DataTableColumnIterator(
            *column_, static_cast<std::size_t>(static_cast<difference_type>(index_) - n));
    }

    DataTableColumnIterator &operator+=(difference_type n) {
        index_ = static_cast<std::size_t>(static_cast<difference_type>(index_) + n);
        return *this;
    }

    DataTableColumnIterator &operator-=(difference_type n) {
        index_ = static_cast<std::size_t>(static_cast<difference_type>(index_) - n);
        return *this;
    }

    friend DataTableColumnIterator operator+(difference_type diff,
                                             const DataTableColumnIterator &other) {
        return other + diff;
    }

    bool operator==(const DataTableColumnIterator &other) const {
        return column_ == other.column_ && index_ == other.index_;
    }

    bool operator!=(const DataTableColumnIterator &other) const {
        return !(*this == other);
    }

    bool operator<(const DataTableColumnIterator &other) const {
        if (column_ != other.column_) {
            throw std::logic_error("Cannot compare iterators from different columns.");
        }

        return index_ < other.index_;
    }

    bool operator>(const DataTableColumnIterator &other) const { return other < *this; }

    bool operator<=(const DataTableColumnIterator &other) const { return !(*this > other); }

    bool operator>=(const DataTableColumnIterator &other) const { return !(*this < other); }

  private:
    const ColumnType *column_;
    std::size_t index_;
};

} // namespace hgps::core