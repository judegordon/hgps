#pragma once
#include "intervention_scenario.h"

#include <functional>
#include <set>
#include <unordered_map>

namespace hgps {

struct PhysicalActivityDefinition {
    PolicyInterval active_period;

    std::vector<PolicyImpact> impacts;

    double coverage_rate{};
};

class PhysicalActivityScenario final : public InterventionScenario {
  public:
    PhysicalActivityScenario() = delete;

    PhysicalActivityScenario(SyncChannel &data_sync, PhysicalActivityDefinition &&definition);

    SyncChannel &channel() override;

    void clear() noexcept override;

    double apply(Random &generator, Person &entity, int time,
                 const core::Identifier &risk_factor_key, double value) override;

    const PolicyInterval &active_period() const noexcept override;

    const std::vector<PolicyImpact> &impacts() const noexcept override;

  private:
    std::reference_wrapper<SyncChannel> channel_;
    PhysicalActivityDefinition definition_;
    std::set<core::Identifier> factor_impact_;
    std::unordered_map<std::size_t, int> interventions_book_{};
};
} // namespace hgps
