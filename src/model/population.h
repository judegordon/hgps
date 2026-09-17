// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/population.h, population.cpp.
#pragma once

#include "person.h"

#include <cstddef>
#include <vector>

namespace hgps::model {

/// @brief The simulated population: a vector of people, with storage slots recycled.
///
/// Three properties, all from docs/decisions/0017-person-ids-monotonic-and-free-slots.md:
///
///  - **Identifiers are lifetime-unique.** A slot may be reused by a newborn; an identifier never
///    is, so the per-person tracking output cannot conflate a dead person with their successor.
///  - **The initial cohort takes identifiers 1..N by index**, so person k of the baseline scenario
///    is person k of the intervention scenario, which is what makes the two futures comparable
///    person by person.
///  - **Free slots are held in an explicit list**, so adding a person is O(1). The baseline
///    rescans from index 0 on every call, once per migrant per age per sex per year, which is
///    quadratic in population size (audit B-13).
class Population {
  public:
    using IteratorType = std::vector<Person>::iterator;
    using ConstIteratorType = std::vector<Person>::const_iterator;

    Population() = delete;

    /// @brief Creates a population of `size` people with identifiers 1..size.
    explicit Population(std::size_t size);

    /// @brief The number of slots, active or not.
    std::size_t size() const noexcept { return people_.size(); }

    std::size_t initial_size() const noexcept { return initial_size_; }

    /// @brief The number of people currently alive and not emigrated.
    ///
    /// Maintained as a counter rather than counted on demand: the baseline counts with
    /// std::count_if(std::execution::par, …) on every call, which is both a parallel algorithm on
    /// a hot path and the reason it needs PSTL at all.
    std::size_t current_active_size() const noexcept { return active_count_; }

    /// @warning No bounds check.
    Person &operator[](std::size_t index) { return people_[index]; }
    const Person &operator[](std::size_t index) const { return people_[index]; }

    /// @throws std::out_of_range outside the population.
    Person &at(std::size_t index) { return people_.at(index); }
    const Person &at(std::size_t index) const { return people_.at(index); }

    /// @brief Adds a person, reusing a free slot when one is available.
    ///
    /// Not noexcept: it may allocate. The baseline declares this noexcept while calling
    /// emplace_back and at(), so a bad_alloc there is std::terminate (audit B-12).
    ///
    /// @param person The person to add; their identifier is assigned here.
    /// @param time The current simulated year. A slot is only reused if its occupant left in an
    ///        earlier year, so this year's deaths are still visible to this year's analysis.
    void add(Person person, unsigned int time);

    /// @brief Adds `number` newborns of one sex, age 0.
    void add_newborn_babies(std::size_t number, core::Gender gender, unsigned int time);

    /// @brief Records that a person has died or emigrated, keeping the active count and the free
    ///        list in step.
    ///
    /// Models call this rather than Person::die directly, because the population owns the
    /// bookkeeping that death implies.
    void mark_died(std::size_t index, unsigned int time);
    void mark_emigrated(std::size_t index, unsigned int time);

    /// @brief Re-derives the active count and free list from the people. For tests, and for any
    ///        caller that has mutated people directly.
    void refresh_bookkeeping(unsigned int time);

    IteratorType begin() noexcept { return people_.begin(); }
    IteratorType end() noexcept { return people_.end(); }
    ConstIteratorType begin() const noexcept { return people_.cbegin(); }
    ConstIteratorType end() const noexcept { return people_.cend(); }
    ConstIteratorType cbegin() const noexcept { return people_.cbegin(); }
    ConstIteratorType cend() const noexcept { return people_.cend(); }

    /// @brief The identifier the next person added will get. For tests and diagnostics.
    std::size_t next_id() const noexcept { return next_person_id_; }

  private:
    std::size_t initial_size_;
    std::size_t next_person_id_{1};
    std::size_t active_count_{0};
    std::vector<Person> people_;

    /// Slot indices whose occupant left in an earlier year, newest first. A stack, so reuse is
    /// O(1); the order in which free slots are handed out does not affect any result, because a
    /// person's identity is their identifier, not their slot.
    std::vector<std::size_t> free_slots_;

    std::size_t allocate_next_person_id() noexcept { return next_person_id_++; }

    /// Moves slots whose occupant left before `time` onto the free list.
    void collect_free_slots(unsigned int time);
};

} // namespace hgps::model
