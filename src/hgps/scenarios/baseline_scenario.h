#pragma once

#include "scenario.h"

namespace hgps {

class BaselineScenario final : public Scenario {
  public:
    BaselineScenario() = default;

    ScenarioType type() const noexcept override;

    const std::string &name() const noexcept override;

    void clear() noexcept override;

    double apply(Random &generator, Person &entity, int time,
                 const core::Identifier &risk_factor_key, double value) override;

  private:
    std::string name_{"Baseline"};
};

} // namespace hgps