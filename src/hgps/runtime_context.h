#pragma once

#include "event_aggregator.h"
#include "mapping.h"
#include "modelinput.h"
#include "population.h"
#include "random_algorithm.h"
#include "runtime_metric.h"
#include "scenario.h"

#include <functional>
#include <vector>

namespace hgps {

class RuntimeContext {
  public:
    RuntimeContext() = delete;
    RuntimeContext(std::shared_ptr<const EventAggregator> bus,
                   std::shared_ptr<const ModelInput> inputs, std::unique_ptr<Scenario> scenario);

    int time_now() const noexcept;

    int start_time() const noexcept;

    unsigned int current_run() const noexcept;

    int sync_timeout_millis() const noexcept;

    Population &population() noexcept;

    const Population &population() const noexcept;

    RuntimeMetric &metrics() noexcept;

    const ModelInput &inputs() const noexcept;

    Scenario &scenario() const noexcept;

    Random &random() const noexcept;

    const HierarchicalMapping &mapping() const noexcept;

    const std::vector<core::DiseaseInfo> &diseases() const noexcept;

    const core::IntegerInterval &age_range() const noexcept;

    const std::string &identifier() const noexcept;

    void set_current_time(const int time_now) noexcept;

    void set_current_run(const unsigned int run_number) noexcept;

    void reset_population(std::size_t initial_pop_size);

    void publish(std::unique_ptr<EventMessage> message) const noexcept;

    void publish_async(std::unique_ptr<EventMessage> message) const noexcept;

  private:
    std::shared_ptr<const EventAggregator> event_bus_;
    std::shared_ptr<const ModelInput> inputs_;
    std::unique_ptr<Scenario> scenario_;
    Population population_;
    mutable Random random_{};
    RuntimeMetric metrics_{};
    unsigned int current_run_{};
    int model_start_time_{};
    int time_now_{};
};

} // namespace hgps
