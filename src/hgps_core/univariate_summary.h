#pragma once
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace hgps::core {

class UnivariateSummary {
  public:
    UnivariateSummary();

    UnivariateSummary(std::string name);

    UnivariateSummary(const std::vector<double> &values);

    UnivariateSummary(std::string name, const std::vector<double> &values);

    std::string name() const noexcept;

    bool is_empty() const noexcept;

    unsigned int count_valid() const noexcept;

    unsigned int count_null() const noexcept;

    unsigned int count_total() const noexcept;

    double min() const noexcept;

    double max() const noexcept;

    double range() const noexcept;

    double sum() const noexcept;

    double average() const noexcept;

    double variance() const noexcept;

    double std_deviation() const noexcept;

    double std_error() const noexcept;

    double kurtosis() const noexcept;

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
    double moments_[5]{};
};
} // namespace hgps::core
