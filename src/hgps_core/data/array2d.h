#pragma once
#include "utils/concepts.h"

#include <algorithm>
#include <cstddef>
#include <fmt/format.h>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace hgps::core {

template <Numerical TYPE>
class Array2D {
  public:
    Array2D() = default;

    Array2D(std::size_t nrows, std::size_t ncols)
        : rows_{nrows}, columns_{ncols}, data_(nrows * ncols) {
        if (nrows == 0 || ncols == 0) {
            throw std::invalid_argument("Invalid array constructor with 0 size");
        }
    }

    Array2D(std::size_t nrows, std::size_t ncols, TYPE value)
        : Array2D(nrows, ncols) {
        fill(value);
    }

    Array2D(std::size_t nrows, std::size_t ncols, const std::vector<TYPE>& values)
        : Array2D(nrows, ncols) {
        if (values.size() != size()) {
            throw std::invalid_argument(fmt::format(
                "Array size and values size mismatch: {} vs. given {}.", size(), values.size()));
        }

        std::copy(values.begin(), values.end(), data_.begin());
    }

    std::size_t size() const noexcept { return rows_ * columns_; }
    std::size_t rows() const noexcept { return rows_; }
    std::size_t columns() const noexcept { return columns_; }

    TYPE& operator()(std::size_t row, std::size_t column) {
        check_boundaries(row, column);
        return data_[row * columns_ + column];
    }

    const TYPE& operator()(std::size_t row, std::size_t column) const {
        check_boundaries(row, column);
        return data_[row * columns_ + column];
    }

    void fill(TYPE value) { std::fill(data_.begin(), data_.end(), value); }
    void clear() { fill(TYPE{}); }

    std::vector<TYPE> to_vector() const { return data_; }

    std::string to_string() const {
        std::ostringstream ss;
        ss << "Array: " << rows_ << "x" << columns_ << "\n";
        for (std::size_t i = 0; i < rows_; i++) {
            ss << "{";
            for (std::size_t j = 0; j < columns_; j++) {
                ss << data_[i * columns_ + j];
                if ((j + 1) != columns_) ss << ", ";
            }
            ss << "}\n";
        }
        return ss.str();
    }

  private:
    std::size_t rows_{};
    std::size_t columns_{};
    std::vector<TYPE> data_;

    void check_boundaries(std::size_t row, std::size_t column) const {
        if (row >= rows_) {
            throw std::out_of_range(
                fmt::format("Row {} is out of array bounds [0, {}).", row, rows_));
        }
        if (column >= columns_) {
            throw std::out_of_range(
                fmt::format("Column {} is out of array bounds [0, {}).", column, columns_));
        }
    }
};

using FloatArray2D = Array2D<float>;
using DoubleArray2D = Array2D<double>;
using IntegerArray2D = Array2D<int>;

} // namespace hgps::core