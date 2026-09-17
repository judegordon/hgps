#include "life_table.h"

#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {

LifeTable::LifeTable(std::map<int, Birth> births, std::map<int, std::map<int, Mortality>> deaths)
    : birth_table_{std::move(births)}, death_table_{std::move(deaths)} {
    // Both halves, over the same years, or not at all. A table with births for 2020-2021 and
    // deaths for 2021 only reads as valid and then throws out_of_range from inside the
    // demographic module's year loop, which is a long way from the file that was wrong. The
    // baseline checks this in its constructor and so does this one.
    if (birth_table_.empty() != death_table_.empty()) {
        throw std::invalid_argument(
            "a life table needs both a birth table and a death table, or neither");
    }

    if (birth_table_.empty()) {
        return;
    }

    time_range_ =
        core::IntegerInterval{birth_table_.begin()->first, birth_table_.rbegin()->first};

    const auto death_range =
        core::IntegerInterval{death_table_.begin()->first, death_table_.rbegin()->first};
    if (death_range.lower() != time_range_.lower() ||
        death_range.upper() != time_range_.upper()) {
        throw std::invalid_argument(fmt::format(
            "a life table's birth and death tables must cover the same years: births cover "
            "{}-{} and deaths cover {}-{}",
            time_range_.lower(), time_range_.upper(), death_range.lower(), death_range.upper()));
    }

    if (!death_table_.begin()->second.empty()) {
        const auto &first_year = death_table_.begin()->second;
        age_range_ = core::IntegerInterval{first_year.begin()->first, first_year.rbegin()->first};
    }
}

const Birth &LifeTable::get_births_at(int time_year) const {
    return birth_table_.at(time_year);
}

const std::map<int, Mortality> &LifeTable::get_mortalities_at(int time_year) const {
    return death_table_.at(time_year);
}

double LifeTable::get_total_deaths_at(int time_year) const {
    // std::map, so ages are summed in ascending order and the total is reproducible.
    double total = 0.0;
    for (const auto &[age, mortality] : death_table_.at(time_year)) {
        total += static_cast<double>(mortality.male) + static_cast<double>(mortality.female);
    }
    return total;
}

bool LifeTable::contains_age(int age) const noexcept { return age_range_.contains(age); }

bool LifeTable::contains_time(int time_year) const noexcept {
    return birth_table_.contains(time_year) && death_table_.contains(time_year);
}

} // namespace hgps::model
