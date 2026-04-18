#include "baseline_scenario.h"

namespace hgps {

ScenarioType BaselineScenario::type() const noexcept { return ScenarioType::baseline; }

const std::string &BaselineScenario::name() const noexcept { return name_; }

void BaselineScenario::clear() noexcept {}

double BaselineScenario::apply([[maybe_unused]] Random &generator,
                               [[maybe_unused]] Person &entity,
                               [[maybe_unused]] int time,
                               [[maybe_unused]] const core::Identifier &risk_factor_key,
                               [[maybe_unused]] double value) {
    return value;
}

} // namespace hgps