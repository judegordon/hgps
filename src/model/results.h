// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{model_result,data_series,runtime_metric}.h and their .cpp files.
#pragma once

#include "containers.h"
#include "core/types.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief A value per sex, as the result tables report it.
struct ResultByGender {
    double male{};
    double female{};

    double &at(core::Gender gender) { return gender == core::Gender::male ? male : female; }
    double at(core::Gender gender) const { return gender == core::Gender::male ? male : female; }
};

/// @brief A value per income category.
struct ResultByIncome {
    double low{};
    double lowermiddle{};
    double middle{};
    double uppermiddle{};
    double high{};

    double &at(core::Income income);
    double at(core::Income income) const;
};

/// @brief A value per income category and sex.
struct ResultByIncomeGender {
    ResultByGender low{};
    ResultByGender lowermiddle{};
    ResultByGender middle{};
    ResultByGender uppermiddle{};
    ResultByGender high{};

    ResultByGender &at(core::Income income);
    const ResultByGender &at(core::Income income) const;
};

/// @brief Years of life lost, years lived with disability, and their sum.
struct DALYsIndicator {
    double years_of_life_lost{};
    double years_lived_with_disability{};
    double disability_adjusted_life_years{};
};

/// @brief Time-series channels by sex, and optionally by income, for one simulation run.
///
/// Every channel holds the same number of points, fixed at construction, so a channel added
/// half-way through cannot be shorter than the others.
class DataSeries {
  public:
    DataSeries() = delete;

    explicit DataSeries(std::size_t sample_size);

    std::size_t size() const noexcept { return sample_size_; }

    /// @brief The channel names, in the order they were added.
    const std::vector<std::string> &channels() const noexcept { return channels_; }

    /// @throws std::logic_error for a duplicate channel name.
    void add_channel(std::string key);
    void add_channels(const std::vector<std::string> &keys);

    bool contains(const std::string &key) const noexcept;

    /// @throws std::out_of_range for an unknown channel.
    std::vector<double> &at(core::Gender gender, const std::string &key);
    const std::vector<double> &at(core::Gender gender, const std::string &key) const;
    std::vector<double> &operator()(core::Gender gender, const std::string &key) {
        return at(gender, key);
    }

    /// @brief The income-stratified channels, created on first use.
    std::vector<double> &at(core::Gender gender, core::Income income, const std::string &key);
    const std::vector<double> &at(core::Gender gender, core::Income income,
                                  const std::string &key) const;

    bool has_income_channels() const noexcept { return !by_income_.empty(); }

  private:
    std::size_t sample_size_;
    std::vector<std::string> channels_;

    // Ordered maps throughout: the result writer iterates these to produce rows, and the row
    // order is part of the output contract (determinism clause D10).
    std::map<core::Gender, std::map<std::string, std::vector<double>>> by_gender_;
    std::map<core::Income, std::map<core::Gender, std::map<std::string, std::vector<double>>>>
        by_income_;
};

/// @brief Named scalar metrics a run reports alongside its results.
class RuntimeMetric {
  public:
    using IteratorType = std::map<std::string, double>::iterator;
    using ConstIteratorType = std::map<std::string, double>::const_iterator;

    std::size_t size() const noexcept { return metrics_.size(); }
    bool empty() const noexcept { return metrics_.empty(); }

    double &operator[](const std::string &key) { return metrics_[key]; }

    /// @throws std::out_of_range for an unknown key.
    double &at(const std::string &key) { return metrics_.at(key); }
    double at(const std::string &key) const { return metrics_.at(key); }

    bool contains(const std::string &key) const noexcept { return metrics_.contains(key); }

    /// @return false if the key was already present.
    bool emplace(const std::string &key, double value);

    bool erase(const std::string &key);

    void clear() noexcept { metrics_.clear(); }

    /// @brief Sets every existing metric to zero, keeping the keys.
    void reset() noexcept;

    IteratorType begin() noexcept { return metrics_.begin(); }
    IteratorType end() noexcept { return metrics_.end(); }
    ConstIteratorType begin() const noexcept { return metrics_.cbegin(); }
    ConstIteratorType end() const noexcept { return metrics_.cend(); }
    ConstIteratorType cbegin() const noexcept { return metrics_.cbegin(); }
    ConstIteratorType cend() const noexcept { return metrics_.cend(); }

  private:
    // std::map, unlike the baseline's unordered_map: the metrics are written to the results file,
    // so their order is output.
    std::map<std::string, double> metrics_;
};

/// @brief Everything one simulated year of one scenario reports.
struct ModelResult {
    ModelResult() = delete;

    explicit ModelResult(std::size_t sample_size);

    int population_size{};
    GenderValue<int> number_alive{};
    int number_dead{};
    int number_emigrated{};
    ResultByGender average_age{};
    DALYsIndicator indicators{};

    std::map<std::string, ResultByGender> risk_factor_average{};
    std::map<std::string, ResultByGender> disease_prevalence{};
    std::map<unsigned int, ResultByGender> comorbidity{};
    std::map<std::string, double> metrics{};

    DataSeries series;

    std::optional<ResultByIncome> population_by_income{};
    std::optional<std::map<std::string, ResultByIncomeGender>> risk_factor_average_by_income{};
    std::optional<std::map<std::string, ResultByIncomeGender>> disease_prevalence_by_income{};
    std::optional<std::map<unsigned int, ResultByIncomeGender>> comorbidity_by_income{};

    std::string to_string() const;
};

} // namespace hgps::model
