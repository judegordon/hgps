#include "runtime_context.h"

namespace hgps {

RuntimeContext::RuntimeContext(std::shared_ptr<const EventAggregator> bus,
                               std::shared_ptr<const ModelInput> inputs,
                               std::unique_ptr<Scenario> scenario)
    : event_bus_{std::move(bus)},
      inputs_{std::move(inputs)},
      scenario_{std::move(scenario)},
      population_{0} {}

int RuntimeContext::time_now() const noexcept { return time_now_; }

int RuntimeContext::start_time() const noexcept { return model_start_time_; }

unsigned int RuntimeContext::current_run() const noexcept { return current_run_; }

int RuntimeContext::sync_timeout_millis() const noexcept { return inputs_->sync_timeout_ms(); }

Population &RuntimeContext::population() noexcept { return population_; }

const Population &RuntimeContext::population() const noexcept { return population_; }

RuntimeMetric &RuntimeContext::metrics() noexcept { return metrics_; }

const ModelInput &RuntimeContext::inputs() const noexcept { return *inputs_; }

Scenario &RuntimeContext::scenario() const noexcept { return *scenario_; }

Random &RuntimeContext::random() noexcept { return random_; }

void RuntimeContext::set_current_time(int time_now) noexcept { time_now_ = time_now; }

void RuntimeContext::set_current_run(unsigned int run_number) noexcept {
    current_run_ = run_number;
}

void RuntimeContext::reset_population(std::size_t initial_pop_size) {
    population_ = Population{initial_pop_size};
    model_start_time_ = inputs_->start_time();
}

void RuntimeContext::publish(std::unique_ptr<EventMessage> message) const noexcept {
    event_bus_->publish(std::move(message));
}

void RuntimeContext::publish_async(std::unique_ptr<EventMessage> message) const noexcept {
    event_bus_->publish_async(std::move(message));
}

} // namespace hgps