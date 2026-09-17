#include "population.h"

#include "diagnostics/internal_error.h"

#include <algorithm>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {

Population::Population(std::size_t size) : initial_size_{size}, active_count_{size} {
    people_.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        // Identifiers 1..N by index, so the baseline and intervention scenarios agree about who
        // person k is.
        people_.emplace_back(i + 1);
        ++next_person_id_;
    }
}

void Population::collect_free_slots(unsigned int time) {
    // A slot is only free if its occupant left in an EARLIER year: this year's deaths and
    // emigrations must remain visible to this year's analysis module.
    for (std::size_t index = 0; index < people_.size(); ++index) {
        const auto &person = people_[index];
        if (person.is_active()) {
            continue;
        }
        // BOTH timestamps must be in the past. One of them is zero for anyone who only died or
        // only emigrated, so testing either alone would free a slot in the same year its
        // occupant left — and this year's analysis has not seen them yet.
        if (person.time_of_death() >= time || person.time_of_migration() >= time) {
            continue;
        }
        if (std::find(free_slots_.begin(), free_slots_.end(), index) == free_slots_.end()) {
            free_slots_.push_back(index);
        }
    }
}

void Population::add(Person person, unsigned int time) {
    person.set_id(allocate_next_person_id());

    if (free_slots_.empty()) {
        collect_free_slots(time);
    }

    if (!free_slots_.empty()) {
        const auto slot = free_slots_.back();
        free_slots_.pop_back();
        people_.at(slot) = std::move(person);
    } else {
        people_.push_back(std::move(person));
    }

    ++active_count_;
}

void Population::add_newborn_babies(std::size_t number, core::Gender gender, unsigned int time) {
    if (number == 0) {
        return;
    }

    if (free_slots_.size() < number) {
        collect_free_slots(time);
    }

    for (std::size_t added = 0; added < number; ++added) {
        Person baby{gender, allocate_next_person_id()};

        if (!free_slots_.empty()) {
            const auto slot = free_slots_.back();
            free_slots_.pop_back();
            people_.at(slot) = std::move(baby);
        } else {
            people_.push_back(std::move(baby));
        }
    }

    active_count_ += number;
}

void Population::mark_died(std::size_t index, unsigned int time) {
    auto &person = people_.at(index);
    if (!person.is_active()) {
        throw diag::InternalError(
            fmt::format("person at slot {} is already inactive and cannot die", index));
    }

    person.die(time);
    --active_count_;
}

void Population::mark_emigrated(std::size_t index, unsigned int time) {
    auto &person = people_.at(index);
    if (!person.is_active()) {
        throw diag::InternalError(
            fmt::format("person at slot {} is already inactive and cannot emigrate", index));
    }

    person.emigrate(time);
    --active_count_;
}

void Population::refresh_bookkeeping(unsigned int time) {
    active_count_ = 0;
    free_slots_.clear();

    for (std::size_t index = 0; index < people_.size(); ++index) {
        const auto &person = people_[index];
        if (person.is_active()) {
            ++active_count_;
            continue;
        }
        if (person.time_of_death() < time && person.time_of_migration() < time) {
            free_slots_.push_back(index);
        }
    }
}

} // namespace hgps::model
