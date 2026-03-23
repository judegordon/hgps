#pragma once
#include "hgps_core/identifier.h"
#include "channel.h"
#include "person.h"
#include "random_algorithm.h"
#include "sync_message.h"

#include <memory>
#include <string>

namespace hgps {

using SyncChannel = Channel<std::unique_ptr<SyncMessage>>;

enum class ScenarioType : uint8_t {
    baseline,

    intervention,
};

class Scenario {
  public:
    virtual ~Scenario() = default;

    virtual ScenarioType type() const noexcept = 0;

    virtual const std::string &name() const noexcept = 0;

    virtual SyncChannel &channel() = 0;

    virtual void clear() noexcept = 0;

    virtual double apply(Random &generator, Person &entity, int time,
                         const core::Identifier &risk_factor_key, double value) = 0;
};
} // namespace hgps
