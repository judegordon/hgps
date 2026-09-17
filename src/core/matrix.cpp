#include "matrix.h"

#include <cmath>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>

namespace hgps::core {

Matrix::Matrix(std::size_t rows, std::size_t columns)
    : rows_{rows}, columns_{columns}, data_(rows * columns, 0.0) {
    if (rows == 0 || columns == 0) {
        throw std::invalid_argument("Invalid matrix constructor with 0 size");
    }
}

double &Matrix::operator()(std::size_t row, std::size_t column) {
    if (row >= rows_ || column >= columns_) {
        throw std::out_of_range(fmt::format("Matrix index ({}, {}) is outside [0, {}) x [0, {}).",
                                            row, column, rows_, columns_));
    }
    return data_[row * columns_ + column];
}

double Matrix::operator()(std::size_t row, std::size_t column) const {
    if (row >= rows_ || column >= columns_) {
        throw std::out_of_range(fmt::format("Matrix index ({}, {}) is outside [0, {}) x [0, {}).",
                                            row, column, rows_, columns_));
    }
    return data_[row * columns_ + column];
}

bool Matrix::is_finite() const noexcept {
    for (const double value : data_) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

bool Matrix::is_symmetric(double tolerance) const noexcept {
    if (rows_ != columns_) {
        return false;
    }
    for (std::size_t i = 0; i < rows_; ++i) {
        for (std::size_t j = i + 1; j < columns_; ++j) {
            if (std::abs(data_[i * columns_ + j] - data_[j * columns_ + i]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

Matrix Matrix::cholesky_lower() const {
    if (rows_ != columns_) {
        throw std::invalid_argument(
            fmt::format("Cholesky decomposition needs a square matrix, got {}x{}.", rows_,
                        columns_));
    }
    if (!is_finite()) {
        throw std::invalid_argument("Cholesky decomposition needs a finite matrix.");
    }

    Matrix lower{rows_, columns_};
    for (std::size_t i = 0; i < rows_; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < j; ++k) {
                sum += lower.data_[i * columns_ + k] * lower.data_[j * columns_ + k];
            }

            if (i == j) {
                const double pivot = data_[i * columns_ + i] - sum;
                if (!(pivot > 0.0)) {
                    throw std::invalid_argument(fmt::format(
                        "Matrix is not positive definite: pivot {} at row {} is not positive.",
                        pivot, i));
                }
                lower.data_[i * columns_ + j] = std::sqrt(pivot);
            } else {
                lower.data_[i * columns_ + j] =
                    (data_[i * columns_ + j] - sum) / lower.data_[j * columns_ + j];
            }
        }
    }

    return lower;
}

std::vector<double> Matrix::multiply(std::span<const double> vector) const {
    if (vector.size() != columns_) {
        throw std::invalid_argument(
            fmt::format("Matrix-vector size mismatch: {} columns vs. a vector of {}.", columns_,
                        vector.size()));
    }

    std::vector<double> result(rows_, 0.0);
    for (std::size_t i = 0; i < rows_; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < columns_; ++j) {
            sum += data_[i * columns_ + j] * vector[j];
        }
        result[i] = sum;
    }

    return result;
}

std::string Matrix::to_string() const {
    std::stringstream ss;
    ss << "Matrix: " << rows_ << "x" << columns_ << "\n";
    for (std::size_t i = 0; i < rows_; ++i) {
        ss << "{";
        for (std::size_t j = 0; j < columns_; ++j) {
            ss << data_[i * columns_ + j];
            if ((j + 1) != columns_) {
                ss << ", ";
            }
        }
        ss << "}\n";
    }
    return ss.str();
}

} // namespace hgps::core
