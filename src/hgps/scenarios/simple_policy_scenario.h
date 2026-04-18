#pragma once

#include "intervention_scenario.h"

#include <map>
#include <vector>

namespace hgps {

enum class PolicyImpactType : uint8_t {
    absolute,
    relative,
};

struct SimplePolicyDefinition {
    SimplePolicyDefinition() = delete;

    SimplePolicyDefinition(const PolicyImpactType &type_of_impact,
                           std::vector<PolicyImpact> sorted_impacts,
                           const PolicyInterval &period)
        : impact_type{type_of_impact},
          impacts{std::move(sorted_impacts)},
          active_period{period} {}

    PolicyImpactType impact_type;
    std::vector<PolicyImpact> impacts;
    PolicyInterval active_period;
};

class SimplePolicyScenario final : public InterventionScenario {
  public:
    SimplePolicyScenario() = delete;

    explicit SimplePolicyScenario(SimplePolicyDefinition &&definition);

    void clear() noexcept override;

    double apply(Random &generator, Person &entity, int time,
                 const core::Identifier &risk_factor_key, double value) override;

    const PolicyImpactType &impact_type() const noexcept;

    const PolicyInterval &active_period() const noexcept override;

    const std::vector<PolicyImpact> &impacts() const noexcept override;

  private:
    SimplePolicyDefinition definition_;
    std::map<core::Identifier, PolicyImpact> factor_impact_;
};

} // namespace hgps