#include "simulation_harness.h"

#include "test_paths.h"

#include <fstream>

namespace hgps::test {

RunOutcome run_simulation(const std::filesystem::path &config_path,
                          const std::filesystem::path &output_folder, std::size_t threads,
                          hgps::api::EventSubscriber *subscriber,
                          const hgps::api::CancellationToken &cancellation) {
    RunOutcome outcome;

    // The host override, not `--output`: the synthetic config names an output folder of its own,
    // and the point of a test run is to run the real config unaltered while putting its results
    // where the test can find them.
    std::filesystem::create_directories(output_folder);

    hgps::api::LoadOptions load_options;
    load_options.output_folder_override = output_folder.string();

    const auto configuration =
        hgps::api::load_configuration(config_path, load_options, outcome.report);
    if (!configuration.has_value()) {
        return outcome;
    }

    const auto data = hgps::api::resolve_data(*configuration, outcome.report);
    if (!data.has_value()) {
        return outcome;
    }

    auto run = hgps::api::build_run(*configuration, *data, outcome.report);
    if (!run.has_value()) {
        return outcome;
    }

    hgps::api::RunOptions run_options;
    run_options.threads = threads;

    const auto summary =
        hgps::api::execute(*run, run_options, subscriber, cancellation, outcome.report);

    outcome.succeeded = summary.succeeded;
    outcome.cancelled = summary.cancelled;
    outcome.years_completed = summary.years_completed;
    outcome.csv_path = summary.result_csv;
    outcome.json_path = summary.result_json;
    outcome.manifest_path = summary.manifest;
    outcome.all_paths = summary.outputs;
    return outcome;
}

RunOutcome run_simulation_perturbed(const std::filesystem::path &config_path,
                                    const std::filesystem::path &output_folder,
                                    const std::string &perturbation) {
    RunOutcome outcome;
    std::filesystem::create_directories(output_folder);

    hgps::api::LoadOptions load_options;
    load_options.output_folder_override = output_folder.string();

    const auto configuration =
        hgps::api::load_configuration(config_path, load_options, outcome.report);
    if (!configuration.has_value()) {
        return outcome;
    }
    const auto data = hgps::api::resolve_data(*configuration, outcome.report);
    if (!data.has_value()) {
        return outcome;
    }
    auto run = hgps::api::build_run(*configuration, *data, outcome.report);
    if (!run.has_value()) {
        return outcome;
    }

    hgps::api::RunOptions run_options;
    run_options.perturbation = perturbation;

    const hgps::api::CancellationToken cancellation;
    const auto summary =
        hgps::api::execute(*run, run_options, nullptr, cancellation, outcome.report);

    outcome.succeeded = summary.succeeded;
    outcome.cancelled = summary.cancelled;
    outcome.years_completed = summary.years_completed;
    outcome.csv_path = summary.result_csv;
    outcome.json_path = summary.result_json;
    outcome.manifest_path = summary.manifest;
    outcome.all_paths = summary.outputs;
    return outcome;
}

nlohmann::json config_document(const FixturePack &pack) {
    std::ifstream stream{pack.config()};
    return nlohmann::json::parse(stream);
}

std::filesystem::path write_config_variant(const FixturePack &pack, const std::string &test_name,
                                           const nlohmann::json &document) {
    const auto directory = scratch_dir(test_name) / "model";
    std::filesystem::copy(pack.directory, directory, std::filesystem::copy_options::recursive);

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
