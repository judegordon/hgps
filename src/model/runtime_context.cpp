#include "runtime_context.h"

#include "sim/scenario.h"

#include <utility>

namespace hgps::model {

RuntimeContext::RuntimeContext(std::shared_ptr<const ModelInput> inputs,
                               std::unique_ptr<sim::Scenario> scenario, std::uint32_t seed)
    : inputs_{std::move(inputs)}, scenario_{std::move(scenario)}, population_{0},
      random_{std::make_unique<rng::RandomSource>(seed)},
      start_time_{static_cast<int>(inputs_->start_time())},
      time_now_{static_cast<int>(inputs_->start_time())} {}

RuntimeContext::~RuntimeContext() = default;

const std::string &RuntimeContext::identifier() const noexcept { return scenario_->name(); }

void RuntimeContext::start_run(unsigned int run_number, std::uint32_t run_seed,
                               std::size_t population_size) {
    current_run_ = run_number;
    time_now_ = start_time_;
    metrics_.clear();
    random_->reseed(run_seed);
    scenario_->clear();

    // A fresh population, so identifiers restart at 1 and person k of this run is person k of the
    // other scenario's run (determinism clause D9).
    population_ = Population{population_size};
}

} // namespace hgps::model
