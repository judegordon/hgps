#include "mapping.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {

MappingEntry::MappingEntry(std::string name, int level, OptionalInterval range)
    : name_{std::move(name)}, key_{name_}, level_{level}, range_{range} {}

double MappingEntry::get_bounded_value(double value) const noexcept {
    if (range_.has_value()) {
        return range_->clamp(value);
    }
    return value;
}

HierarchicalMapping::HierarchicalMapping(std::vector<MappingEntry> mapping)
    : mapping_{std::move(mapping)} {
    for (const auto &entry : mapping_) {
        max_level_ = std::max(max_level_, entry.level());
    }
}

const MappingEntry &HierarchicalMapping::at(const core::Identifier &key) const {
    const auto found = std::find_if(mapping_.begin(), mapping_.end(),
                                    [&key](const MappingEntry &entry) {
                                        return entry.key() == key;
                                    });
    if (found == mapping_.end()) {
        throw std::out_of_range(
            fmt::format("risk factor '{}' is not in the model's mapping", key.to_string()));
    }
    return *found;
}

bool HierarchicalMapping::contains(const core::Identifier &key) const noexcept {
    return std::any_of(mapping_.begin(), mapping_.end(),
                       [&key](const MappingEntry &entry) { return entry.key() == key; });
}

std::vector<MappingEntry> HierarchicalMapping::at_level(int level) const {
    std::vector<MappingEntry> result;
    for (const auto &entry : mapping_) {
        if (entry.level() == level) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<core::Identifier> HierarchicalMapping::keys() const {
    std::vector<core::Identifier> result;
    result.reserve(mapping_.size());
    for (const auto &entry : mapping_) {
        result.push_back(entry.key());
    }
    return result;
}

} // namespace hgps::model
