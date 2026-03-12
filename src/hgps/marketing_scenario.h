#pragma once
#include "intervention_scenario.h"

#include <functional>
#include <set>

namespace hgps {

struct MarketingPolicyDefinition {
    MarketingPolicyDefinition() = delete;
    MarketingPolicyDefinition(const PolicyInterval &period,
                              std::vector<PolicyImpact> sorted_impacts)
        : active_period{period}, impacts{std::move(sorted_impacts)} {}

    PolicyInterval active_period;

    std::vector<PolicyImpact> impacts;
};

class MarketingPolicyScenario final : public InterventionScenario {
  public:
    MarketingPolicyScenario() = delete;

    MarketingPolicyScenario(SyncChannel &data_sync, MarketingPolicyDefinition &&definition);

    SyncChannel &channel() override;

    void clear() noexcept override;

    double apply(Random &generator, Person &entity, int time,
                 const core::Identifier &risk_factor_key, double value) override;

    const PolicyInterval &active_period() const noexcept override;

    const std::vector<PolicyImpact> &impacts() const noexcept override;

  private:
    std::reference_wrapper<SyncChannel> channel_;
    MarketingPolicyDefinition definition_;
    std::set<core::Identifier> factor_impact_;
    std::unordered_map<std::size_t, int> interventions_book_{};
};
} // namespace hgps
