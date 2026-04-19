#pragma once

#include "events/event_aggregator.h"
#include "events/channel.h"
#include "events/sync_message.h"
#include "data/model_input.h"
#include "data/population.h"
#include "utils/random_algorithm.h"
#include "runtime_metric.h"
#include "scenarios/scenario.h"

#include <functional>
#include <memory>

namespace hgps {

using SyncChannel = Channel<std::unique_ptr<SyncMessage>>;

class RuntimeContext {
  public:
    RuntimeContext() = delete;
    RuntimeContext(std::shared_ptr<const EventAggregator> bus,
                   std::shared_ptr<const ModelInput> inputs,
                   std::unique_ptr<Scenario> scenario,
                   SyncChannel &sync_channel);

    int time_now() const noexcept;
    int start_time() const noexcept;
    unsigned int current_run() const noexcept;
    int sync_timeout_millis() const noexcept;

    Population &population() noexcept;
    const Population &population() const noexcept;

    RuntimeMetric &metrics() noexcept;

    const ModelInput &inputs() const noexcept;
    Scenario &scenario() const noexcept;
    Random &random() noexcept;
    SyncChannel &sync_channel() noexcept;

    void set_current_time(int time_now) noexcept;
    void set_current_run(unsigned int run_number) noexcept;
    void reset_population(std::size_t initial_pop_size);

    void publish(std::unique_ptr<EventMessage> message) const noexcept;
    void publish_async(std::unique_ptr<EventMessage> message) const noexcept;

  private:
    std::shared_ptr<const EventAggregator> event_bus_;
    std::shared_ptr<const ModelInput> inputs_;
    std::unique_ptr<Scenario> scenario_;
    std::reference_wrapper<SyncChannel> sync_channel_;
    Population population_;
    Random random_{};
    RuntimeMetric metrics_{};
    unsigned int current_run_{};
    int model_start_time_{};
    int time_now_{};
};

} // namespace hgps