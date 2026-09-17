#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace hgps::core {

/// @brief A small dense double matrix with the two operations the risk-factor models need.
///
/// This replaces Eigen, which the baseline pulls in for one Cholesky decomposition of the
/// risk-factor correlation matrix and one matrix-vector product
/// (docs/decisions/0023-own-matrix-and-cholesky.md). Both operations here have a stated
/// summation order, because the order decides the last bits of every correlated residual and
/// therefore belongs to this codebase rather than to a library's expression evaluator.
class Matrix {
  public:
    Matrix() = default;

    /// @throws std::invalid_argument for a zero-sized dimension.
    Matrix(std::size_t rows, std::size_t columns);

    std::size_t rows() const noexcept { return rows_; }
    std::size_t columns() const noexcept { return columns_; }
    bool empty() const noexcept { return data_.empty(); }

    /// @throws std::out_of_range for an index outside the matrix.
    double &operator()(std::size_t row, std::size_t column);
    double operator()(std::size_t row, std::size_t column) const;

    bool is_finite() const noexcept;
    bool is_symmetric(double tolerance) const noexcept;

    /// @brief The lower-triangular Cholesky factor L of this matrix, so that L * Lᵀ == *this.
    ///
    /// Cholesky-Banachiewicz, computed row by row in ascending index order, each inner product
    /// summed in ascending index order.
    ///
    /// @throws std::invalid_argument if the matrix is not square, or not positive definite — the
    ///         message names the offending row, which is the diagnostic a correlation matrix that
    ///         is not positive definite actually needs. The baseline only checked allFinite()
    ///         after the fact.
    Matrix cholesky_lower() const;

    /// @brief This matrix times a vector, each row summed in ascending column order.
    /// @throws std::invalid_argument if vector.size() != columns().
    std::vector<double> multiply(std::span<const double> vector) const;

    std::string to_string() const;

  private:
    std::size_t rows_{};
    std::size_t columns_{};
    std::vector<double> data_;
};

} // namespace hgps::core
