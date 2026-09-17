// healthgps — the command-line host.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Console/program.cpp.
#include "build_modules.h"
#include "options.h"

#include "config/loader.h"
#include "core/parallel.h"
#include "data/store.h"
#include "diagnostics/internal_error.h"
#include "io/data_source.h"
#include "output/result_writer.h"
#include "random/seed.h"
#include "sim/engine.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace {

constexpr const char *kProgramName = "healthgps";
constexpr const char *kProgramVersion = "0.1.0";

/// Exit codes, so a script can tell the kinds of failure apart.
enum class ExitCode : int {
    success = 0,
    usage = 2,
    input_problem = 3,
    internal_error = 70,
};

int to_int(ExitCode code) { return static_cast<int>(code); }

/// Prints a report and says whether it is fatal.
bool report_and_check(const hgps::diag::IssueReport &report) {
    if (!report.empty()) {
        std::cerr << report.to_string();
    }
    return !report.has_errors();
}

} // namespace

int main(int argc, char **argv) {
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    auto parsed = hgps::app::parse_options(arguments);
    if (!parsed.options.has_value()) {
        std::cerr << kProgramName << ": " << parsed.message << "\n\n"
                  << hgps::app::usage_text();
        return to_int(ExitCode::usage);
    }

    const auto &options = *parsed.options;

    if (options.help) {
        std::cout << hgps::app::usage_text();
        return to_int(ExitCode::success);
    }
    if (options.version) {
        std::cout << kProgramName << ' ' << kProgramVersion << '\n';
        return to_int(ExitCode::success);
    }

    try {
        // Set once, before anything runs a parallel region.
        hgps::core::parallel::set_worker_count(options.threads);

        hgps::diag::IssueReport report;

        // 1. The config. Everything wrong with it is reported together.
        hgps::config::LoadOptions load_options;
        load_options.output_folder = options.output_folder;
        load_options.job_id = options.job_id;
        load_options.verbose = options.verbose;

        const auto config = hgps::config::load(options.config, load_options, report);
        if (!config.has_value()) {
            report_and_check(report);
            std::cerr << kProgramName << ": the configuration has errors; nothing was run.\n";
            return to_int(ExitCode::input_problem);
        }

        // 2. The data store: fetched and verified if it is remote, then its index and disease
        //    registry validated.
        const hgps::io::DataSource source{config->data.source, config->data.checksum,
                                          config->root_path};
        const auto data_directory = source.resolve(report);
        if (!data_directory.has_value()) {
            report_and_check(report);
            return to_int(ExitCode::input_problem);
        }

        const auto store = hgps::data::Store::open(*data_directory, report);
        if (!store.has_value() || report.has_errors()) {
            report_and_check(report);
            return to_int(ExitCode::input_problem);
        }

        // 3. Everything a run needs, loaded before any scenario starts.
        const auto loaded = hgps::app::load_inputs(*config, *store, report);
        if (!loaded.has_value()) {
            report_and_check(report);
            return to_int(ExitCode::input_problem);
        }

        // 4. The modules, one set per scenario.
        hgps::sim::ScenarioJournal journal;

        auto baseline_modules = hgps::app::build_modules(*loaded, *config, journal, report);
        if (!baseline_modules.has_value()) {
            report_and_check(report);
            return to_int(ExitCode::input_problem);
        }

        std::optional<hgps::sim::Modules> intervention_modules;
        if (config->running.active_intervention.has_value()) {
            intervention_modules = hgps::app::build_modules(*loaded, *config, journal, report);
            if (!intervention_modules.has_value()) {
                report_and_check(report);
                return to_int(ExitCode::input_problem);
            }
        }

        // Warnings are worth seeing even on a successful run.
        report_and_check(report);

        if (options.dry_run) {
            std::cout << fmt::format(
                "{}: configuration, models and data validated. {} disease{}, {} risk factor{}, "
                "cohort of {} people, {}–{}.\n",
                kProgramName, loaded->inputs->diseases().size(),
                loaded->inputs->diseases().size() == 1 ? "" : "s",
                loaded->inputs->risk_mapping().size(),
                loaded->inputs->risk_mapping().size() == 1 ? "" : "s", loaded->cohort_size,
                config->running.start_time, config->running.stop_time);
            return to_int(ExitCode::success);
        }

        // 5. The output files.
        hgps::output::RunMetadata metadata;
        metadata.model = kProgramName;
        metadata.version = kProgramVersion;
        metadata.intervention = config->running.active_intervention.has_value()
                                    ? config->running.active_intervention->identifier
                                    : "";
        metadata.job_id = config->job_id;
        metadata.seed = config->running.seed;
        for (unsigned int run = 0; run < config->running.trial_runs; ++run) {
            metadata.run_seeds.push_back(hgps::rng::derive_run_seed(config->running.seed, run));
        }
        metadata.config_path = config->source_path;
        metadata.config_sha256 = config->source_sha256;
        metadata.country = loaded->inputs->settings().country.name;
        metadata.start_time = config->running.start_time;
        metadata.stop_time = config->running.stop_time;
        metadata.trial_runs = config->running.trial_runs;
        metadata.cohort_size = loaded->cohort_size;

        const auto output_path =
            std::filesystem::path{config->output.folder} /
            hgps::config::expand_output_file_name(config->output, config->job_id);

        hgps::output::ResultWriter writer{output_path, metadata,
                                          loaded->inputs->income_analysis_enabled(),
                                          loaded->inputs->income_layout()};

        // 6. The run. Scenarios are sequential, and the writer sees rows in scenario then year
        //    order — which is what makes the output byte-comparable between runs (ADR 0020).
        hgps::sim::Engine baseline{loaded->inputs,
                                    std::make_unique<hgps::sim::BaselineScenario>(),
                                    std::move(*baseline_modules), config->running.seed};

        hgps::sim::Runner runner;
        const auto sink = [&writer](const hgps::sim::ResultRow &row) { writer.write(row); };

        double elapsed_ms = 0.0;
        if (intervention_modules.has_value()) {
            hgps::sim::Engine intervention{
                loaded->inputs,
                hgps::sim::create_intervention_scenario(*config->running.active_intervention),
                std::move(*intervention_modules), config->running.seed};

            elapsed_ms = runner.run(baseline, intervention, config->running.trial_runs,
                                    config->running.seed, sink);
        } else {
            elapsed_ms =
                runner.run(baseline, config->running.trial_runs, config->running.seed, sink);
        }

        writer.close();

        std::cout << fmt::format("{}: {} run{} of {}–{}, cohort {}, seed {}, {:.1f}s\n",
                                  kProgramName, config->running.trial_runs,
                                  config->running.trial_runs == 1 ? "" : "s",
                                  config->running.start_time, config->running.stop_time,
                                  loaded->cohort_size, config->running.seed,
                                  elapsed_ms / 1000.0);
        for (const auto &path : writer.paths()) {
            std::cout << "  " << path.string() << '\n';
        }

        return to_int(ExitCode::success);
    } catch (const hgps::diag::InternalError &error) {
        // A broken invariant in this program, not a problem with the user's input.
        std::cerr << kProgramName << ": internal error: " << error.describe() << '\n';
        return to_int(ExitCode::internal_error);
    } catch (const std::exception &error) {
        std::cerr << kProgramName << ": " << error.what() << '\n';
        return to_int(ExitCode::internal_error);
    }
}
