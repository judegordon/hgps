#include "factor_values.h"

#include <algorithm>
#include <stdexcept>

#include <fmt/format.h>

namespace hgps::model {

std::uint32_t FactorIndex::find(const core::Identifier &name) const noexcept {
    const auto found = by_name_.find(name);
    return found == by_name_.end() ? unknown : found->second;
}

std::uint32_t FactorIndex::intern(const core::Identifier &name) {
    const auto found = by_name_.find(name);
    if (found != by_name_.end()) {
        return found->second;
    }

    const auto index = static_cast<std::uint32_t>(by_index_.size());
    by_index_.push_back(name);
    by_name_.emplace(name, index);
    return index;
}

const core::Identifier &FactorIndex::name_of(std::uint32_t index) const {
    if (index >= by_index_.size()) {
        throw std::out_of_range(
            fmt::format("risk-factor index {} was never assigned a name", index));
    }
    return by_index_[index];
}

void FactorIndex::clear() noexcept {
    by_name_.clear();
    by_index_.clear();
}

FactorIndex &factor_index() {
    static FactorIndex index;
    return index;
}

// Below this many entries a linear scan of 32-bit integers beats a binary search, and above it the
// search wins. Measured rather than assumed, on the two examples that have profiles
// (docs/decisions/0040-a-bounded-search-for-the-long-vectors.md): a person carries at most **6**
// entries on `HLM_France` and up to **121** on `KevinHall_FINCH`, so the two sit two orders of
// magnitude apart and any threshold between them divides them the same way. Sixteen is a round
// number in that gap, chosen so that France's lookup stays exactly the scan it was.
constexpr std::size_t kScanLimit = 16;

std::size_t FactorValues::position_of(std::uint32_t index) const noexcept {
    const auto count = entries_.size();

    // A short vector: scan it. Contiguous, an integer comparison, one or two cache lines, and no
    // mispredicted branch worth the name — which is what ADR 0037 said about the scan and is still
    // true at this size.
    if (count <= kScanLimit) {
        for (std::size_t position = 0; position < count; ++position) {
            if (entries_[position].index == index) {
                return position;
            }
        }
        return count;
    }

    // A long one: binary search, over a prefix rather than the whole vector. `entries_` is sorted by
    // index with distinct indices, so entry *k* has an index of at least *k*; a position *p* holding
    // `index` therefore needs *p* <= `index`, and nothing above that can hold it.
    const auto last = std::min(static_cast<std::size_t>(index) + 1, count);
    const auto first = entries_.begin();
    const auto end = first + static_cast<std::ptrdiff_t>(last);
    const auto found = std::lower_bound(first, end, index,
                                        [](const Entry &entry, std::uint32_t value) {
                                            return entry.index < value;
                                        });
    if (found != end && found->index == index) {
        return static_cast<std::size_t>(found - first);
    }
    return count;
}

double &FactorValues::operator[](const core::Identifier &name) {
    const auto index = factor_index().intern(name);
    const auto position = position_of(index);
    if (position != entries_.size()) {
        return entries_[position].value;
    }

    // Kept sorted by index, so iteration has a stated order and two people with the same factors
    // iterate them the same way.
    const auto where = std::lower_bound(entries_.begin(), entries_.end(), index,
                                        [](const Entry &entry, std::uint32_t value) {
                                            return entry.index < value;
                                        });
    return entries_.insert(where, Entry{.index = index, .value = 0.0})->value;
}

double &FactorValues::at_index(std::uint32_t index) {
    const auto position = position_of(index);
    if (position == entries_.size()) {
        throw std::out_of_range(fmt::format("no value for risk-factor index {}", index));
    }
    return entries_[position].value;
}

double FactorValues::at_index(std::uint32_t index) const {
    const auto position = position_of(index);
    if (position == entries_.size()) {
        throw std::out_of_range(fmt::format("no value for risk-factor index {}", index));
    }
    return entries_[position].value;
}

const double *FactorValues::find_index(std::uint32_t index) const noexcept {
    const auto position = position_of(index);
    return position == entries_.size() ? nullptr : &entries_[position].value;
}

double *FactorValues::find_index(std::uint32_t index) noexcept {
    const auto position = position_of(index);
    return position == entries_.size() ? nullptr : &entries_[position].value;
}

double &FactorValues::at(const core::Identifier &name) {
    const auto index = factor_index().find(name);
    if (index == FactorIndex::unknown) {
        throw std::out_of_range(fmt::format("no risk factor named '{}'", name.to_string()));
    }
    return at_index(index);
}

double FactorValues::at(const core::Identifier &name) const {
    const auto index = factor_index().find(name);
    if (index == FactorIndex::unknown) {
        throw std::out_of_range(fmt::format("no risk factor named '{}'", name.to_string()));
    }
    return at_index(index);
}

bool FactorValues::contains(const core::Identifier &name) const noexcept {
    const auto index = factor_index().find(name);
    return index != FactorIndex::unknown && find_index(index) != nullptr;
}

FactorValues::iterator FactorValues::find(const core::Identifier &name) noexcept {
    const auto index = factor_index().find(name);
    if (index == FactorIndex::unknown) {
        return end();
    }
    return iterator{this, position_of(index)};
}

FactorValues::const_iterator FactorValues::find(const core::Identifier &name) const noexcept {
    const auto index = factor_index().find(name);
    if (index == FactorIndex::unknown) {
        return end();
    }
    return const_iterator{this, position_of(index)};
}

FactorValues::iterator::Entry FactorValues::entry_at(std::size_t position) {
    auto &entry = entries_.at(position);
    return iterator::Entry{factor_index().name_of(entry.index), entry.value};
}

FactorValues::const_iterator::Entry FactorValues::entry_at(std::size_t position) const {
    const auto &entry = entries_.at(position);
    return const_iterator::Entry{factor_index().name_of(entry.index), entry.value};
}

} // namespace hgps::model
