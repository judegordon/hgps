// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/runtime_context.h, runtime_context.cpp.
#pragma once

#include "mapping.h"
#include "model_input.h"
#include "population.h"
#include "random/source.h"
#include "results.h"

#include <memory>
#include <string>
#include <vector>

namespace hgps::sim {
class Scenario;
} // namespace hgps::sim

namespace hgps::model {

/// @brief The world one scenario run operates on: the population, the clock, the metrics and the
///        single random stream.
///
/// Owns exactly one rng::RandomSource, handed out by reference. That type is neither copyable nor
/// movable and every draw checks the parallel-region guard, so there is no way to reach an RNG
/// from a worker thread (determinism clauses D2 and D3).
///
/// There is no event bus here. The baseline's context publishes results asynchronously onto a
/// concurrent queue, which is what makes its output row order depend on thread scheduling
/// (audit B-01); results are returned to the runner instead
/// (docs/decisions/0020-output-single-owner-defined-row-order.md).
class RuntimeContext {
  public:
    RuntimeContext() = delete;

    RuntimeContext(std::shared_ptr<const ModelInput> inputs,
                   std::unique_ptr<sim::Scenario> scenario, std::uint32_t seed);

    ~RuntimeContext();
    RuntimeContext(const RuntimeContext &) = delete;
    RuntimeContext &operator=(const RuntimeContext &) = delete;
    RuntimeContext(RuntimeContext &&) = delete;
    RuntimeContext &operator=(RuntimeContext &&) = delete;

    int time_now() const noexcept { return time_now_; }
    int start_time() const noexcept { return start_time_; }
    unsigned int current_run() const noexcept { return current_run_; }

    Population &population() noexcept { return population_; }
    const Population &population() const noexcept { return population_; }

    RuntimeMetric &metrics() noexcept { return metrics_; }
    const RuntimeMetric &metrics() const noexcept { return metrics_; }

    const ModelInput &inputs() const noexcept { return *inputs_; }

    sim::Scenario &scenario() const noexcept { return *scenario_; }

    /// @brief The run's single random stream.
    rng::RandomSource &random() const noexcept { return *random_; }

    const HierarchicalMapping &mapping() const noexcept { return inputs_->risk_mapping(); }
    const std::vector<core::DiseaseInfo> &diseases() const noexcept { return inputs_->diseases(); }
    const core::IntegerInterval &age_range() const noexcept {
        return inputs_->settings().age_range;
    }

    /// @brief The scenario's name, for output and diagnostics.
    const std::string &identifier() const noexcept;

    void set_current_time(int time_now) noexcept { time_now_ = time_now; }
    void set_current_run(unsigned int run_number) noexcept { current_run_ = run_number; }

    /// @brief Starts a fresh run: a new population, a re-seeded stream, cleared metrics.
    void start_run(unsigned int run_number, std::uint32_t run_seed, std::size_t population_size);

  private:
    std::shared_ptr<const ModelInput> inputs_;
    std::unique_ptr<sim::Scenario> scenario_;
    Population population_;

    // A pointer, because RandomSource is deliberately immovable and the population size — and so
    // the run — is not known at construction. std::optional would do as well; a unique_ptr keeps
    // the header free of the source's definition.
    std::unique_ptr<rng::RandomSource> random_;

    RuntimeMetric metrics_{};
    unsigned int current_run_{};
    int start_time_{};
    int time_now_{};
};

} // namespace hgps::model
