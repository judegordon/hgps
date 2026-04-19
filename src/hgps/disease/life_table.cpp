#include "life_table.h"

#include <fmt/format.h>

#include <numeric>
#include <stdexcept>
#include <utility>

namespace hgps {

LifeTable::LifeTable(std::map<int, Birth> &&births, std::map<int, std::map<int, Mortality>> &&deaths)
    : birth_table_{std::move(births)}, death_table_{std::move(deaths)} {
    if (birth_table_.empty()) {
        if (!death_table_.empty()) {
            throw std::invalid_argument("empty births and deaths content mismatch");
        }

        return;
    }

    if (death_table_.empty()) {
        throw std::invalid_argument("births and empty deaths content mismatch");
    }

    if (birth_table_.size() != death_table_.size()) {
        throw std::invalid_argument("births and deaths time dimension mismatch");
    }

    auto birth_it = birth_table_.begin();
    auto death_it = death_table_.begin();
    for (; birth_it != birth_table_.end() && death_it != death_table_.end(); ++birth_it, ++death_it) {
        if (birth_it->first != death_it->first) {
            throw std::invalid_argument("births and deaths time range mismatch");
        }
    }

    time_range_ = core::IntegerInterval(death_table_.begin()->first, death_table_.rbegin()->first);

    const auto &reference_ages = death_table_.begin()->second;
    if (reference_ages.empty()) {
        throw std::invalid_argument("life table mortality data must not contain empty age rows");
    }

    age_range_ =
        core::IntegerInterval(reference_ages.begin()->first, reference_ages.rbegin()->first);

    for (const auto &[year, ages] : death_table_) {
        if (ages.empty()) {
            throw std::invalid_argument(
                fmt::format("life table mortality data is empty for year {}", year));
        }

        if (ages.size() != reference_ages.size()) {
            throw std::invalid_argument(
                fmt::format("life table age dimension mismatch for year {}", year));
        }

        auto ref_it = reference_ages.begin();
        auto age_it = ages.begin();
        for (; ref_it != reference_ages.end() && age_it != ages.end(); ++ref_it, ++age_it) {
            if (ref_it->first != age_it->first) {
                throw std::invalid_argument(
                    fmt::format("life table age keys mismatch for year {}", year));
            }
        }
    }
}

const Birth &LifeTable::get_births_at(const int time_year) const { return birth_table_.at(time_year); }

const std::map<int, Mortality> &LifeTable::get_mortalities_at(const int time_year) const {
    return death_table_.at(time_year);
}

double LifeTable::get_total_deaths_at(const int time_year) const {
    if (!death_table_.contains(time_year)) {
        throw std::out_of_range("The year must not be outside of the data limits.");
    }

    const auto &year_data = get_mortalities_at(time_year);
    return std::accumulate(year_data.cbegin(), year_data.cend(), 0.0,
                           [](const double previous, const auto &element) {
                               return previous + element.second.total();
                           });
}

bool LifeTable::contains_age(const int age) const noexcept {
    return !empty() && age_range_.lower() <= age && age <= age_range_.upper();
}

bool LifeTable::contains_time(const int time_year) const noexcept {
    return death_table_.contains(time_year);
}

const core::IntegerInterval &LifeTable::time_limits() const noexcept { return time_range_; }

const core::IntegerInterval &LifeTable::age_limits() const noexcept { return age_range_; }

bool LifeTable::empty() const noexcept { return birth_table_.empty(); }

} // namespace hgps