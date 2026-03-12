#pragma once
#include "intervention_scenario.h"

#include <functional>
#include <set>
#include <vector>

namespace hgps {

struct MarketingDynamicDefinition {
    MarketingDynamicDefinition() = delete;

    MarketingDynamicDefinition(const PolicyInterval &period,
                               std::vector<PolicyImpact> sorted_impacts, PolicyDynamic dynamic)
        : active_period{period}, impacts{std::move(sorted_impacts)}, dynamic{dynamic} {}

    PolicyInterval active_period;

    std::vector<PolicyImpact> impacts;

    PolicyDynamic dynamic;
};

class MarketingDynamicScenario final : public DynamicInterventionScenario {
  public:
    MarketingDynamicScenario() = delete;

    MarketingDynamicScenario(SyncChannel &data_sync, MarketingDynamicDefinition &&definition);

    SyncChannel &channel() override;

    void clear() noexcept override;

    double apply(Random &generator, Person &entity, int time,
                 const core::Identifier &risk_factor_key, double value) override;

    const PolicyInterval &active_period() const noexcept override;

    const std::vector<PolicyImpact> &impacts() const noexcept override;

    const PolicyDynamic &dynamic() const noexcept override;

  private:
    std::reference_wrapper<SyncChannel> channel_;
    MarketingDynamicDefinition definition_;
    std::set<core::Identifier> factor_impact_;
    std::unordered_map<std::size_t, int> interventions_book_{};

    int get_index_of_impact_by_age(unsigned int age) const;
    double get_differential_impact(int current_index, int previous_index) const;
};
} // namespace hgps
