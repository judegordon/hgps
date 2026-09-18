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

std::size_t FactorValues::position_of(std::uint32_t index) const noexcept {
    // A linear scan, on purpose. These vectors hold eleven entries on HLM_France and fifty-five on
    // FINCH, they are contiguous, and the comparison is a 32-bit integer — so the whole scan is one
    // or two cache lines with no branch misprediction worth the name. A binary search wins at
    // hundreds of entries, which no configuration has.
    for (std::size_t position = 0; position < entries_.size(); ++position) {
        if (entries_[position].index == index) {
            return position;
        }
    }
    return entries_.size();
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
