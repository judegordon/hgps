#include "reporter.h"

#include <fmt/format.h>

namespace hgps::app {

ConsoleReporter::ConsoleReporter(std::ostream &out, std::ostream &progress_stream, bool progress)
    : out_{out}, progress_{progress_stream}, show_progress_{progress} {}

void ConsoleReporter::on_run_started(const api::RunStarted &event) {
    total_years_ = event.total_years;
    seed_ = event.seed;
    trial_runs_ = event.trial_runs;
    start_time_ = event.start_time;
    stop_time_ = event.stop_time;
    cohort_size_ = event.cohort_size;

    if (!show_progress_) {
        return;
    }

    std::string scenarios;
    for (const auto &name : event.scenarios) {
        if (!scenarios.empty()) {
            scenarios += ", ";
        }
        scenarios += name;
    }
    progress_ << fmt::format("healthgps {}: {}–{}, cohort {}, seed {}, {} run{}, scenarios: {}\n",
                             event.engine_version, event.start_time, event.stop_time,
                             event.cohort_size, event.seed, event.trial_runs,
                             event.trial_runs == 1 ? "" : "s", scenarios);
}

void ConsoleReporter::on_scenario_started(const api::ScenarioStarted &event) {
    if (!show_progress_) {
        return;
    }
    progress_ << fmt::format("  run {} {}: started\n", event.run, event.scenario);
}

void ConsoleReporter::on_year_completed(const api::YearCompleted &event) {
    ++years_done_;
    if (!show_progress_) {
        return;
    }
    // The denominator comes from the run-started event rather than being recomputed, so the two
    // cannot disagree.
    progress_ << fmt::format("  [{}/{}] run {} {} {}: {} people, {:.0f} ms\n", years_done_,
                             total_years_, event.run, event.scenario, event.year,
                             event.population_size, event.elapsed_ms);
}

void ConsoleReporter::on_scenario_completed(const api::ScenarioCompleted &event) {
    if (!show_progress_) {
        return;
    }
    progress_ << fmt::format("  run {} {}: {} year{} in {:.1f}s\n", event.run, event.scenario,
                             event.years_completed, event.years_completed == 1 ? "" : "s",
                             event.elapsed_ms / 1000.0);
}

void ConsoleReporter::on_run_completed(const api::RunCompleted &event) {
    out_ << fmt::format("healthgps: {} run{} of {}–{}, cohort {}, seed {}, {:.1f}s{}\n",
                        trial_runs_, trial_runs_ == 1 ? "" : "s", start_time_, stop_time_,
                        cohort_size_, seed_, event.elapsed_ms / 1000.0,
                        event.cancelled ? " (cancelled)" : "");
    for (const auto &path : event.outputs) {
        out_ << "  " << path.string() << '\n';
    }
}

void ConsoleReporter::on_diagnostic(const api::Diagnostic &diagnostic) {
    progress_ << diagnostic.to_string() << '\n';
}

std::string version_text(const std::string &program_name) {
    const auto &build = api::build_info();
    return fmt::format("{} {}\n"
                       "  commit    {}{}\n"
                       "  describe  {}\n"
                       "  platform  {}\n"
                       "  compiler  {}\n"
                       "  build     {}\n",
                       program_name, build.version, build.git_commit,
                       build.git_dirty ? " (modified working tree)" : "", build.git_describe,
                       build.platform, build.compiler,
                       build.build_type.empty() ? "unspecified" : build.build_type);
}

std::string dry_run_text(const std::string &program_name, const api::Run::Description &run) {
    return fmt::format("{}: configuration, models and data validated. {} disease{}, {} risk "
                       "factor{}, cohort of {} people, {}–{}, seed {}.\n",
                       program_name, run.disease_count, run.disease_count == 1 ? "" : "s",
                       run.risk_factor_count, run.risk_factor_count == 1 ? "" : "s",
                       run.cohort_size, run.start_time, run.stop_time, run.seed);
}

} // namespace hgps::app
