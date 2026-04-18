#include "runner.h"
#include "events/runner_message.h"

#include <chrono>
#include <fmt/format.h>

namespace hgps {

using ElapsedTime = std::chrono::duration<double, std::milli>;

namespace {
double run_simulation_series(EventAggregator &event_bus, RandomBitGenerator &seed_generator,
                             std::stop_source &source, const std::string &runner_id,
                             Simulation &baseline, Simulation *intervention,
                             unsigned int trial_runs) {
    source = std::stop_source{};

    auto notify = [&](std::unique_ptr<hgps::EventMessage> message) {
        event_bus.publish_async(std::move(message));
    };

    auto run_one = [&](Simulation &model, unsigned int run, unsigned int seed) {
        auto run_start = std::chrono::steady_clock::now();

        notify(std::make_unique<RunnerEventMessage>(
            fmt::format("{} - {}", runner_id, model.name()), RunnerAction::run_begin, run));

        adevs::Simulator<int> sim;
        model.setup_run(run, seed);
        sim.add(&model);

        while (!source.stop_requested() && sim.next_event_time() < adevs_inf<adevs::Time>()) {
            sim.exec_next_event();
        }

        ElapsedTime elapsed = std::chrono::steady_clock::now() - run_start;
        notify(std::make_unique<RunnerEventMessage>(
            fmt::format("{} - {}", runner_id, model.name()), RunnerAction::run_end, run,
            elapsed.count()));
    };

    auto start = std::chrono::steady_clock::now();
    notify(std::make_unique<RunnerEventMessage>(runner_id, RunnerAction::start));

    for (auto run = 1u; run <= trial_runs; ++run) {
        const unsigned int run_seed = seed_generator.next();

        run_one(baseline, run, run_seed);

        if (source.stop_requested()) {
            notify(std::make_unique<RunnerEventMessage>(runner_id, RunnerAction::cancelled));
            break;
        }

        if (intervention != nullptr) {
            run_one(*intervention, run, run_seed);

            if (source.stop_requested()) {
                notify(std::make_unique<RunnerEventMessage>(runner_id, RunnerAction::cancelled));
                break;
            }
        }
    }

    ElapsedTime elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsed_ms = elapsed.count();
    notify(std::make_unique<RunnerEventMessage>(runner_id, RunnerAction::finish, elapsed_ms));
    return elapsed_ms;
}
} // namespace

Runner::Runner(std::shared_ptr<EventAggregator> bus,
               std::unique_ptr<RandomBitGenerator> seed_generator) noexcept
    : running_{false}, event_bus_{std::move(bus)}, seed_generator_{std::move(seed_generator)} {}

double Runner::run(Simulation &baseline, const unsigned int trial_runs) {
    if (trial_runs < 1) {
        throw std::invalid_argument("The number of trial runs must not be less than one");
    }
    if (baseline.type() != ScenarioType::baseline) {
        throw std::invalid_argument(
            fmt::format("Simulation: '{}' cannot be evaluated alone", baseline.name()));
    }
    if (running_.load()) {
        throw std::invalid_argument("The model runner is already evaluating an experiment");
    }

    running_.store(true);
    const auto reset = make_finally([this]() { running_.store(false); });

    return run_simulation_series(*event_bus_, *seed_generator_, source_, "single_runner",
                                 baseline, nullptr, trial_runs);
}

double Runner::run(Simulation &baseline, Simulation &intervention, const unsigned int trial_runs) {
    if (trial_runs < 1) {
        throw std::invalid_argument("The number of trial runs must not be less than one.");
    }
    if (baseline.type() != ScenarioType::baseline) {
        throw std::invalid_argument(
            fmt::format("Baseline simulation: {} type mismatch.", baseline.name()));
    }
    if (intervention.type() != ScenarioType::intervention) {
        throw std::invalid_argument(
            fmt::format("Intervention simulation: {} type mismatch.", intervention.name()));
    }
    if (running_.load()) {
        throw std::invalid_argument("The model runner is already evaluating an experiment.");
    }

    running_.store(true);
    const auto reset = make_finally([this]() { running_.store(false); });

    return run_simulation_series(*event_bus_, *seed_generator_, source_, "paired_runner", baseline,
                                 &intervention, trial_runs);
}

bool Runner::is_running() const noexcept { return running_.load(); }

void Runner::cancel() noexcept {
    if (is_running()) {
        source_.request_stop();
    }
}

void Runner::run_model_thread(const std::stop_token &, Simulation &, unsigned int,
                              const unsigned int) {}

void Runner::notify(std::unique_ptr<hgps::EventMessage> message) {
    event_bus_->publish_async(std::move(message));
}

} // namespace hgps