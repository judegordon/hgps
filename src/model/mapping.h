// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/mapping.h, mapping.cpp.
#pragma once

#include "core/identifier.h"
#include "core/interval.h"

#include <optional>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief The identifier of the constant term in a regression model.
inline const core::Identifier kInterceptKey{"intercept"};

using OptionalInterval = std::optional<core::DoubleInterval>;

/// @brief One risk factor: its name, the hierarchy level it is generated at, and its range.
class MappingEntry {
  public:
    MappingEntry() = delete;

    MappingEntry(std::string name, int level, OptionalInterval range = {});

    const std::string &name() const noexcept { return name_; }
    int level() const noexcept { return level_; }
    const core::Identifier &key() const noexcept { return key_; }
    const OptionalInterval &range() const noexcept { return range_; }

    /// @brief The value clamped to this factor's range, or unchanged if it has none.
    double get_bounded_value(double value) const noexcept;

  private:
    std::string name_;
    core::Identifier key_;
    int level_{};
    OptionalInterval range_;
};

/// @brief The risk factors of a model, in the order the config declares them.
///
/// The order matters twice over: risk factors are generated level by level, and within a level
/// the generation order is the declaration order, which makes the RNG draw sequence a property of
/// the config rather than of a container.
class HierarchicalMapping {
  public:
    using IteratorType = std::vector<MappingEntry>::iterator;
    using ConstIteratorType = std::vector<MappingEntry>::const_iterator;

    HierarchicalMapping() = delete;

    explicit HierarchicalMapping(std::vector<MappingEntry> mapping);

    const std::vector<MappingEntry> &entries() const noexcept { return mapping_; }
    std::size_t size() const noexcept { return mapping_.size(); }

    /// @brief The highest level present, or 0 for an empty mapping.
    int max_level() const noexcept { return max_level_; }

    /// @throws std::out_of_range if no entry has that key.
    const MappingEntry &at(const core::Identifier &key) const;

    bool contains(const core::Identifier &key) const noexcept;

    /// @brief The entries at one level, in declaration order.
    std::vector<MappingEntry> at_level(int level) const;

    /// @brief Every entry's key, in declaration order. This is the set that model loading
    ///        validates coefficient names against (ADR 0018).
    std::vector<core::Identifier> keys() const;

    IteratorType begin() noexcept { return mapping_.begin(); }
    IteratorType end() noexcept { return mapping_.end(); }
    ConstIteratorType begin() const noexcept { return mapping_.cbegin(); }
    ConstIteratorType end() const noexcept { return mapping_.cend(); }
    ConstIteratorType cbegin() const noexcept { return mapping_.cbegin(); }
    ConstIteratorType cend() const noexcept { return mapping_.cend(); }

  private:
    std::vector<MappingEntry> mapping_;
    int max_level_{0};
};

} // namespace hgps::model
