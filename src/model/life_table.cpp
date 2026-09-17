#include "life_table.h"

#include <utility>

namespace hgps::model {

LifeTable::LifeTable(std::map<int, Birth> births, std::map<int, std::map<int, Mortality>> deaths)
    : birth_table_{std::move(births)}, death_table_{std::move(deaths)} {
    if (!birth_table_.empty()) {
        time_range_ = core::IntegerInterval{birth_table_.begin()->first,
                                            birth_table_.rbegin()->first};
    }

    if (!death_table_.empty() && !death_table_.begin()->second.empty()) {
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
