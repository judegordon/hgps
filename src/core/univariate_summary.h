// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/univariate_summary.h, univariate_summary.cpp.
#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace hgps::core {

/// @brief A running univariate statistical summary in constant space.
///
/// Uses statistical moments updated per value, so nothing is stored. Null values are counted but
/// take no part in any calculation. The moment recurrence is the baseline's, unchanged: the ported
/// tests assert its exact values, and it is numerically the better choice over naive sums anyway.
class UnivariateSummary {
  public:
    UnivariateSummary();
    explicit UnivariateSummary(std::string name);
    explicit UnivariateSummary(const std::vector<double> &values);
    UnivariateSummary(std::string name, const std::vector<double> &values);

    const std::string &name() const noexcept;
    bool is_empty() const noexcept;

    unsigned int count_valid() const noexcept;
    unsigned int count_null() const noexcept;
    unsigned int count_total() const noexcept;

    double min() const noexcept;
    double max() const noexcept;
    double range() const noexcept;
    double sum() const noexcept;
    double average() const noexcept;

    /// @return The sample variance, or NaN with fewer than two valid values.
    double variance() const noexcept;
    double std_deviation() const noexcept;
    double std_error() const noexcept;

    /// @return The kurtosis, or NaN with fewer than four valid values.
    double kurtosis() const noexcept;

    /// @return The skewness, or NaN with fewer than three valid values.
    double skewness() const noexcept;

    void clear() noexcept;

    void append(double value) noexcept;
    void append(const std::optional<double> &option) noexcept;
    void append(const std::vector<double> &values) noexcept;
    void append_null() noexcept;
    void append_null(unsigned int count) noexcept;

    std::string to_string() const noexcept;

    friend std::ostream &operator<<(std::ostream &stream, const UnivariateSummary &summary);

  private:
    std::string name_;
    unsigned int null_count_{};
    double min_{};
    double max_{};

    // moments_[0] is the valid count, [1] the mean, [2..4] the central moments of order 2..4.
    double moments_[5]{};
};

} // namespace hgps::core
