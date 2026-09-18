// Part of the public API of hgps::engine. Nothing here includes an internal header.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
#pragma once

#include "diagnostics.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hgps::api {

/// @brief Which of the two futures a scenario is.
enum class ScenarioKind {
    baseline,
    intervention,
};

std::string_view to_string(ScenarioKind kind) noexcept;

/// @brief The run is about to start. Everything here is already validated and fixed.
struct RunStarted {
    std::string engine_version;

    /// @brief The master seed the run will use — the one actually used, not one it might have had.
    std::uint32_t seed{};

    unsigned int trial_runs{};
    int start_time{};
    int stop_time{};
    std::size_t cohort_size{};

    /// @brief The scenario names, in the order they will run.
    std::vector<std::string> scenarios;

    /// @brief How many `YearCompleted` events the run will emit if it is not cancelled, so a
    ///        progress bar has a denominator before the first year finishes.
    std::size_t total_years{};
};

struct ScenarioStarted {
    std::string scenario;
    ScenarioKind kind{};

    /// @brief The 1-based trial run number.
    unsigned int run{};
};

/// @brief One simulated year finished. The unit of progress.
struct YearCompleted {
    std::string scenario;
    ScenarioKind kind{};
    unsigned int run{};
    int year{};

    /// @brief Wall-clock milliseconds this year took.
    double elapsed_ms{};

    /// @brief How many people were alive and present at the end of the year.
    std::size_t population_size{};
};

struct ScenarioCompleted {
    std::string scenario;
    ScenarioKind kind{};
    unsigned int run{};
    double elapsed_ms{};

    /// @brief How many years this scenario actually simulated. Short of the horizon if cancelled.
    std::size_t years_completed{};
};

struct RunCompleted {
    double elapsed_ms{};

    /// @brief True if the run stopped early because its cancellation token was set.
    bool cancelled{};

    /// @brief Every file written, including the manifest.
    std::vector<std::filesystem::path> outputs;
};

/// @brief Receives a run's progress events.
///
/// Every method has an empty default, so a subscriber implements only what it uses. The engine
/// itself writes nothing to stdout or stderr: printing is the caller's job, and this is how it
/// gets what to print.
///
/// **Ordering and threading.** Events are delivered synchronously, from the thread that called
/// `execute()`, in the order the run produced them. A subscriber therefore needs no locking, and
/// it also blocks the run for as long as it takes — so a slow subscriber is a slow simulation.
///
/// **Events cannot change results.** Nothing the engine emits is derived from the random stream
/// and nothing a subscriber does is visible to the simulation: there is no way for a subscriber to
/// reach the RNG, the population or the work order. `tests/engine/event_stream_test.cpp` asserts
/// that a run with a subscriber and the same run without one write byte-identical results.
class EventSubscriber {
  public:
    EventSubscriber() = default;
    virtual ~EventSubscriber() = default;
    EventSubscriber(const EventSubscriber &) = delete;
    EventSubscriber &operator=(const EventSubscriber &) = delete;
    EventSubscriber(EventSubscriber &&) = delete;
    EventSubscriber &operator=(EventSubscriber &&) = delete;

    virtual void on_run_started(const RunStarted &) {}
    virtual void on_scenario_started(const ScenarioStarted &) {}
    virtual void on_year_completed(const YearCompleted &) {}
    virtual void on_scenario_completed(const ScenarioCompleted &) {}
    virtual void on_run_completed(const RunCompleted &) {}

    /// @brief A warning or informational diagnostic raised while the run was in progress.
    ///
    /// Input problems are reported by the load and build calls, which is where they belong. This
    /// is for what only running can discover — an age band immigration cannot refill, for
    /// instance. An error raised here has already stopped the run.
    virtual void on_diagnostic(const Diagnostic &) {}
};

} // namespace hgps::api
