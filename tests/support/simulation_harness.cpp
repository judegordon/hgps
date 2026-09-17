#include "simulation_harness.h"

#include "test_paths.h"

#include "app/build_modules.h"
#include "config/loader.h"
#include "core/parallel.h"
#include "data/store.h"
#include "io/data_source.h"
#include "output/result_writer.h"
#include "random/seed.h"
#include "sim/engine.h"

#include <fstream>

namespace hgps::test {

RunOutcome run_simulation(const std::filesystem::path &config_path,
                          const std::filesystem::path &output_folder, std::size_t threads) {
    RunOutcome outcome;

    const hgps::core::parallel::WorkerCountScope workers{threads};

    // The config is loaded as it stands and then its output folder is replaced, rather than
    // passing --output: giving a folder in both places is an error, and the point of the harness
    // is to run the real config unaltered.
    auto config = hgps::config::load(config_path, hgps::config::LoadOptions{}, outcome.report);
    if (!config.has_value()) {
        return outcome;
    }

    std::filesystem::create_directories(output_folder);
    config->output.folder = output_folder.string();

    const hgps::io::DataSource source{config->data.source, config->data.checksum,
                                      config->root_path};
    const auto data_directory = source.resolve(outcome.report);
    if (!data_directory.has_value()) {
        return outcome;
    }

    const auto store = hgps::data::Store::open(*data_directory, outcome.report);
    if (!store.has_value() || outcome.report.has_errors()) {
        return outcome;
    }

    const auto loaded = hgps::app::load_inputs(*config, *store, outcome.report);
    if (!loaded.has_value()) {
        return outcome;
    }

    hgps::sim::ScenarioJournal journal;

    auto baseline_modules = hgps::app::build_modules(*loaded, *config, journal, outcome.report);
    if (!baseline_modules.has_value()) {
        return outcome;
    }

    std::optional<hgps::sim::Modules> intervention_modules;
    if (config->running.active_intervention.has_value()) {
        intervention_modules = hgps::app::build_modules(*loaded, *config, journal, outcome.report);
        if (!intervention_modules.has_value()) {
            return outcome;
        }
    }

    hgps::output::RunMetadata metadata;
    metadata.model = "healthgps";
    metadata.version = "test";
    metadata.intervention = config->running.active_intervention.has_value()
                                ? config->running.active_intervention->identifier
                                : "";
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

    {
        hgps::output::ResultWriter writer{output_path, metadata,
                                          loaded->inputs->income_analysis_enabled(),
                                          loaded->inputs->income_layout()};

        hgps::sim::Engine baseline{loaded->inputs,
                                    std::make_unique<hgps::sim::BaselineScenario>(),
                                    std::move(*baseline_modules), config->running.seed};

        hgps::sim::Runner runner;
        const auto sink = [&writer](const hgps::sim::ResultRow &row) { writer.write(row); };

        if (intervention_modules.has_value()) {
            hgps::sim::Engine intervention{
                loaded->inputs,
                hgps::sim::create_intervention_scenario(*config->running.active_intervention),
                std::move(*intervention_modules), config->running.seed};
            runner.run(baseline, intervention, config->running.trial_runs, config->running.seed,
                       sink);
        } else {
            runner.run(baseline, config->running.trial_runs, config->running.seed, sink);
        }

        outcome.csv_path = writer.csv_path();
        outcome.json_path = writer.json_path();
        outcome.all_paths = writer.paths();
    }

    outcome.succeeded = true;
    return outcome;
}

nlohmann::json synthetic_config_document() {
    std::ifstream stream{synthetic_config()};
    return nlohmann::json::parse(stream);
}

std::filesystem::path write_config_variant(const std::string &test_name,
                                           const nlohmann::json &document) {
    const auto directory = scratch_dir(test_name) / "model";
    std::filesystem::copy(synthetic_model_dir(), directory,
                          std::filesystem::copy_options::recursive);

    // The variant's data source has to point at the pack's data half, which the copy moved away
    // from.
    auto copy = document;
    copy["data"]["source"] = synthetic_data_dir().string();

    const auto path = directory / "config.json";
    std::ofstream stream{path, std::ios::trunc};
    stream << copy.dump(2);
    return path;
}

} // namespace hgps::test
