// The CLI's subscriber: it turns the engine's events into lines on a terminal.
//
// Everything printed about a run is printed here. The library writes to neither stream — it cannot,
// there is no stream in its API — so this file is the whole of the program's output behaviour
// besides diagnostics (docs/decisions/0033-an-event-stream-the-simulation-cannot-see.md).
#pragma once

#include "hgps/engine.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace hgps::app {

/// @brief Prints a run's progress.
///
/// Progress goes to the error stream and the summary to the output stream, so that piping the
/// summary somewhere does not swallow the progress and a progress line does not corrupt a summary
/// somebody is parsing.
class ConsoleReporter final : public api::EventSubscriber {
  public:
    /// @param progress Print a line per simulated year. Off by default: a forty-year run at one
    ///        trial produces forty lines, which is noise in a log and useful on a terminal.
    ConsoleReporter(std::ostream &out, std::ostream &progress_stream, bool progress);

    void on_run_started(const api::RunStarted &event) override;
    void on_scenario_started(const api::ScenarioStarted &event) override;
    void on_year_completed(const api::YearCompleted &event) override;
    void on_scenario_completed(const api::ScenarioCompleted &event) override;
    void on_run_completed(const api::RunCompleted &event) override;
    void on_diagnostic(const api::Diagnostic &diagnostic) override;

  private:
    std::ostream &out_;
    std::ostream &progress_;
    bool show_progress_;

    std::size_t total_years_{0};
    std::size_t years_done_{0};
    std::uint32_t seed_{0};
    unsigned int trial_runs_{0};
    int start_time_{0};
    int stop_time_{0};
    std::size_t cohort_size_{0};
};

/// @brief `--version` output: the program name, the engine version and what built it.
std::string version_text(const std::string &program_name);

/// @brief `--dry-run` output: what the run would do, now that everything is validated.
std::string dry_run_text(const std::string &program_name, const api::Run::Description &run);

} // namespace hgps::app
