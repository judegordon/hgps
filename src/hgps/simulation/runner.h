#pragma once

#include "events/event_aggregator.h"
#include "simulation.h"

#include <atomic>
#include <memory>
#include <stop_token>

namespace hgps {

class Runner {
  public:
    Runner() = delete;

    Runner(std::shared_ptr<EventAggregator> bus,
           std::unique_ptr<RandomBitGenerator> seed_generator) noexcept;

    double run(Simulation &baseline, unsigned int trial_runs);
    double run(Simulation &baseline, Simulation &intervention, unsigned int trial_runs);

    bool is_running() const noexcept;
    void cancel() noexcept;

  private:
    std::atomic<bool> running_;
    std::shared_ptr<EventAggregator> event_bus_;
    std::unique_ptr<RandomBitGenerator> seed_generator_;
    std::stop_source source_;

    void notify(std::unique_ptr<EventMessage> message);
};

} // namespace hgps