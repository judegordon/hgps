#pragma once
#include "hgps_core/types/identifier.h"
#include "hgps_core/types/interval.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace hgps {

inline const core::Identifier InterceptKey = core::Identifier{"intercept"};

using OptionalInterval = std::optional<core::DoubleInterval>;

class MappingEntry {
  public:
    MappingEntry() = delete;

    MappingEntry(std::string name, int level, OptionalInterval range = {});

    const std::string &name() const noexcept;

    int level() const noexcept;

    const core::Identifier &key() const noexcept;

    const OptionalInterval &range() const noexcept;

    double get_bounded_value(double value) const noexcept;

  private:
    std::string name_;
    core::Identifier name_key_;
    int level_{};
    OptionalInterval range_;
};

class HierarchicalMapping {
  public:
    using IteratorType = std::vector<MappingEntry>::iterator;
    using ConstIteratorType = std::vector<MappingEntry>::const_iterator;

    HierarchicalMapping() = delete;

    HierarchicalMapping(std::vector<MappingEntry> mapping);

    const std::vector<MappingEntry> &entries() const noexcept;

    std::size_t size() const noexcept;

    int max_level() const noexcept;

    MappingEntry at(const core::Identifier &key) const;

    std::vector<MappingEntry> at_level(int level) const noexcept;

    IteratorType begin() noexcept { return mapping_.begin(); }

    IteratorType end() noexcept { return mapping_.end(); }

    ConstIteratorType begin() const noexcept { return mapping_.cbegin(); }

    ConstIteratorType end() const noexcept { return mapping_.cend(); }

    ConstIteratorType cbegin() const noexcept { return mapping_.cbegin(); }

    ConstIteratorType cend() const noexcept { return mapping_.cend(); }

  private:
    std::vector<MappingEntry> mapping_;
};

} // namespace hgps
