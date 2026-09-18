// An index-keyed store for a person's risk-factor values, and the name-to-index table behind it.
//
// Both profiles in docs/performance.md found the same thing, and FINCH said it louder: 40% of
// HLM_France's samples and 52% of KevinHall_FINCH's were **string comparison**, nearly all of it
// `std::map` lookups keyed by `core::Identifier`. `Identifier` compares by string deliberately —
// comparing by the cached hash is audit finding B-04 — and every risk-factor read on every person in
// every year was such a comparison, through a red-black tree.
//
// So a name is resolved to a dense index **once, at load**, and the hot paths use the index
// (docs/decisions/0037-index-keyed-risk-factor-store.md). The store is a small flat vector sorted by
// index: a scan of a dozen 32-bit integers beats a tree of string comparisons at these sizes, and it
// is one allocation per person instead of one per factor.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
#pragma once

#include "core/identifier.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hgps::model {

/// @brief The run-wide assignment of risk-factor names to dense indices.
///
/// Append-only within a run: an index, once handed out, means the same name until the table is
/// cleared. That is what lets a `FactorValues` hold bare indices and a model cache one.
///
/// Every name a run will use is interned while the run is being built, so no simulated year interns
/// anything. `intern` is still available afterwards because tests construct people directly, and
/// because a name arriving late is better resolved than refused.
class FactorIndex {
  public:
    /// @brief The value `find` returns for a name this table has never seen.
    static constexpr std::uint32_t unknown = 0xFFFFFFFFU;

    /// @brief The index of a name, or `unknown`. Never assigns.
    std::uint32_t find(const core::Identifier &name) const noexcept;

    /// @brief The index of a name, assigning the next free one if it is new.
    std::uint32_t intern(const core::Identifier &name);

    /// @throws std::out_of_range for an index this table has not handed out.
    const core::Identifier &name_of(std::uint32_t index) const;

    std::size_t size() const noexcept { return by_index_.size(); }

    /// @brief Forgets every name. For tests; a run never calls it.
    void clear() noexcept;

  private:
    // unordered_map is correct here and is not audit finding B-05: nothing iterates this table, so
    // its bucket order cannot reach a result. `std::hash<Identifier>` is the cached 64-bit hash, and
    // a collision is the container's problem, which it handles by comparing the keys — with
    // `Identifier::operator==`, which compares the string. That is the distinction B-04 is about.
    std::unordered_map<core::Identifier, std::uint32_t> by_name_;
    std::vector<core::Identifier> by_index_;
};

/// @brief The index every `FactorValues` in this process shares.
///
/// Process-wide, because a `Person` is copied and cloned constantly and a back-pointer on every one
/// of a million people would cost more than it buys. Two simulations must not run at once in one
/// process anyway — the worker pool and the parallel-region guard are also process-wide, and
/// docs/api.md says so — and a second run in the same process simply interns its own names, which is
/// correct because the table is append-only.
FactorIndex &factor_index();

/// @brief One person's risk-factor values, keyed by index and presenting the surface of a map.
///
/// Deliberately map-shaped: `find`, `at`, `operator[]`, `contains`, `size`, and iteration all mean
/// what they mean on `std::map<Identifier, double>`, so the change from one to the other is a change
/// of representation and not of every call site.
///
/// **Iteration is in index order, which is not name order.** That is the one observable difference,
/// and it matters in exactly one place: a product over a person's factors, where the order decides
/// the last bits. That site iterates its own name-ordered list instead
/// ([ADR 0037](../../docs/decisions/0037-index-keyed-risk-factor-store.md)).
class FactorValues {
  public:
    FactorValues() = default;

    template <bool Const> class Iterator {
      public:
        using Value = std::conditional_t<Const, const double, double>;

        struct Entry {
            const core::Identifier &first;
            Value &second;
        };

        /// @brief A pointer-like, so `found->second` works as it does on a map iterator.
        struct Pointer {
            Entry entry;
            Entry *operator->() { return &entry; }
        };

        using Store = std::conditional_t<Const, const FactorValues, FactorValues>;

        Iterator() = default;
        Iterator(Store *store, std::size_t position) : store_{store}, position_{position} {}

        Entry operator*() const { return store_->entry_at(position_); }
        Pointer operator->() const { return Pointer{store_->entry_at(position_)}; }

        Iterator &operator++() {
            ++position_;
            return *this;
        }

        bool operator==(const Iterator &other) const noexcept {
            return store_ == other.store_ && position_ == other.position_;
        }

      private:
        Store *store_{nullptr};
        std::size_t position_{0};
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    /// @brief The value for a name, inserting a zero if it is not there — as `std::map` does.
    double &operator[](const core::Identifier &name);

    /// @brief The value for an index already known to be present.
    double &at_index(std::uint32_t index);
    double at_index(std::uint32_t index) const;

    /// @brief The value for an index, inserting a zero if it is not there — `operator[]` without
    ///        the name.
    ///
    /// The writing half of the hot-path pair. `operator[]` has to intern the name first, which is a
    /// hash probe and an identifier comparison; a model that resolved its names when it was built
    /// hands in the index instead. The index must have come from `factor_index()`, or iteration
    /// would report a name that is not this value's.
    double &at_index_or_insert(std::uint32_t index);

    /// @brief The value for an index, or null when this person has no value for it.
    ///
    /// The hot-path accessor: no name, no hash, no string. A model resolves its names to indices once
    /// and calls this per person per year.
    const double *find_index(std::uint32_t index) const noexcept;
    double *find_index(std::uint32_t index) noexcept;

    /// @throws std::out_of_range if the name is not present, as `std::map::at` does.
    double &at(const core::Identifier &name);
    double at(const core::Identifier &name) const;

    bool contains(const core::Identifier &name) const noexcept;

    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

    iterator find(const core::Identifier &name) noexcept;
    const_iterator find(const core::Identifier &name) const noexcept;

    iterator begin() noexcept { return iterator{this, 0}; }
    iterator end() noexcept { return iterator{this, entries_.size()}; }
    const_iterator begin() const noexcept { return const_iterator{this, 0}; }
    const_iterator end() const noexcept { return const_iterator{this, entries_.size()}; }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

    void clear() noexcept { entries_.clear(); }

  private:
    struct Entry {
        std::uint32_t index;
        double value;
    };

    // Sorted by index, and short: eleven entries on HLM_France, thirty-four plus twenty-one food
    // groups on FINCH. A linear scan over that is faster than a binary search's branches, and both
    // are faster than a tree of string comparisons.
    std::vector<Entry> entries_;

    std::size_t position_of(std::uint32_t index) const noexcept;

    typename iterator::Entry entry_at(std::size_t position);
    typename const_iterator::Entry entry_at(std::size_t position) const;

    template <bool Const> friend class Iterator;
};

} // namespace hgps::model
