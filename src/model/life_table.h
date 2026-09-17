// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/life_table.h, life_table.cpp.
#pragma once

#include "containers.h"
#include "core/interval.h"

#include <map>

namespace hgps::model {

/// @brief Deaths by sex.
using Mortality = GenderValue<float>;

/// @brief Births in a year.
struct Birth {
    Birth() = default;
    Birth(float number_of_births, float birth_sex_ratio)
        : number{number_of_births}, sex_ratio{birth_sex_ratio} {}

    float number{};

    /// @brief Males per 100 female births.
    float sex_ratio{};
};

/// @brief The population's births and deaths by year and age.
class LifeTable {
  public:
    LifeTable() = delete;

    LifeTable(std::map<int, Birth> births, std::map<int, std::map<int, Mortality>> deaths);

    /// @throws std::out_of_range for a year the table does not cover.
    const Birth &get_births_at(int time_year) const;

    /// @throws std::out_of_range for a year the table does not cover.
    const std::map<int, Mortality> &get_mortalities_at(int time_year) const;

    /// @brief Total deaths in a year, summed in ascending age order.
    double get_total_deaths_at(int time_year) const;

    bool contains_age(int age) const noexcept;
    bool contains_time(int time_year) const noexcept;

    const core::IntegerInterval &time_limits() const noexcept { return time_range_; }
    const core::IntegerInterval &age_limits() const noexcept { return age_range_; }

    bool empty() const noexcept { return birth_table_.empty() || death_table_.empty(); }

  private:
    std::map<int, Birth> birth_table_;
    std::map<int, std::map<int, Mortality>> death_table_;
    core::IntegerInterval time_range_{};
    core::IntegerInterval age_range_{};
};

} // namespace hgps::model
