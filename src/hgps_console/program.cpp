#include "hgps_core/utils/thread_util.h"
#include "hgps_input/config/config.h"
#include "hgps_input/data/data_manager.h"
#include "hgps_input/io/csv_parser.h"
#include "hgps_input/models/model_parser.h"
#include "hgps/data/model_input.h"
#include "hgps/events/event_bus.h"
#include "hgps/scenarios/baseline_scenario.h"
#include "hgps/simulation/runner.h"
#include "hgps/simulation/simulation.h"
#include "hgps/simulation/simulation_module.h"
#include "hgps/utils/mt_random.h"

#include "command_options.h"
#include "event_monitor.h"
#include "individual_id_tracking_writer.h"
#include "model_info.h"
#include "result_file_writer.h"

#include <fmt/color.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <oneapi/tbb/global_control.h>
#include <optional>
#include <stdexcept>

namespace {

constexpr unsigned int DEFAULT_GLOBAL_SEED = 12345u;

unsigned int resolve_effective_seed(const hgps::ModelInput &input) {
    return input.seed().value_or(DEFAULT_GLOBAL_SEED);
}

void print_app_title() {
    fmt::print(fg(fmt::color::yellow) | fmt::emphasis::bold,
               "\n# Health-GPS Microsimulation for Policy Options #\n\n");

    fmt::print("Maximum threads: {}\n\n",
               tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism));
}

hgps::ResultFileWriter create_results_file_logger(const hgps::input::Configuration &config,
                                                  const hgps::ModelInput &input) {
    const bool write_income_csv = input.project_requirements().income.income_based_csv_output;

    return {create_output_file_name(config.output, config.job_id),
            hgps::ExperimentInfo{.model = config.app_name,
                                 .version = config.app_version,
                                 .intervention = config.active_intervention
                                                     ? config.active_intervention->identifier
                                                     : "",
                                 .job_id = config.job_id,
                                 .seed = resolve_effective_seed(input)},
            write_income_csv};
}

int exit_application(int exit_code) {
    fmt::print("\n\n");
    fmt::print(fg(fmt::color::yellow) | fmt::emphasis::bold, "Goodbye.\n\n");
    return exit_code;
}

} // namespace

int main(int argc, char *argv[]) { // NOLINT(bugprone-exception-escape)
    using namespace hgps;
    using namespace hgps::input;

    auto options = create_options();
    if (argc < 2) {
        std::cout << options.help() << '\n';
        return exit_application(EXIT_FAILURE);
    }

    std::optional<CommandOptions> cmd_args_opt;
    try {
        cmd_args_opt = parse_arguments(options, argc, argv);
        if (!cmd_args_opt) {
            return exit_application(EXIT_SUCCESS);
        }
    } catch (const std::exception &ex) {
        fmt::print(fg(fmt::color::red), "\nInvalid command line argument: {}\n", ex.what());
        fmt::print("\n{}\n", options.help());
        return exit_application(EXIT_FAILURE);
    }

    const auto &cmd_args = cmd_args_opt.value();

    std::optional<tbb::global_control> thread_control;
    if (cmd_args.num_threads != 0) {
        thread_control.emplace(tbb::global_control::max_allowed_parallelism, cmd_args.num_threads);
    }

    print_app_title();

    Configuration config;
    try {
        config = get_configuration(cmd_args.config_source, cmd_args.output_folder, cmd_args.job_id,
                                   cmd_args.verbose);
    } catch (const std::exception &ex) {
        fmt::print(fg(fmt::color::red), "\n\nInvalid configuration - {}.\n", ex.what());
        return exit_application(EXIT_FAILURE);
    }

#ifdef CATCH_EXCEPTIONS
    try {
#endif
        if (cmd_args.data_source.has_value() == (config.data_source != nullptr)) {
            fmt::print(
                fg(fmt::color::red),
                "Must provide a data source via config file or command line, but not both\n");
            return exit_application(EXIT_FAILURE);
        }

        const auto &data_source =
            cmd_args.data_source.has_value() ? *cmd_args.data_source : *config.data_source;

        auto data_api = input::DataManager(data_source.get_data_directory(), config.verbosity);
        auto data_repository = hgps::CachedRepository{data_api};

        hgps::core::InputIssueReport model_diagnostics;
        register_risk_factor_model_definitions(data_repository, config, model_diagnostics);
        if (model_diagnostics.has_errors()) {
            for (const auto &diagnostic : model_diagnostics) {
                fmt::print(fmt::fg(fmt::color::red), "{}\n",
                           hgps::core::format_issue(diagnostic));
            }
            return exit_application(EXIT_FAILURE);
        }

        auto factory = get_default_simulation_module_factory(data_repository);

        auto country = data_api.get_country(config.settings.country);
        fmt::print("Target country: {} - {}, population: {:0.3g}%.\n", country.code, country.name,
                   config.settings.size_fraction * 100.0f);

        auto diseases = get_diseases_info(data_api, config);
        if (diseases.size() != config.diseases.size()) {
            fmt::print(fg(fmt::color::red), "\nInvalid list of diseases in configuration.\n");
            return exit_application(EXIT_FAILURE);
        }

        if (cmd_args.dry_run) {
            fmt::print(fg(fmt::color::yellow), "Dry run completed successfully.\n");
            return exit_application(EXIT_SUCCESS);
        }

        auto table_future = core::run_async(load_datatable_from_csv, config.file);
        auto input_table = table_future.get();

        std::cout << input_table;

        auto model_input = std::make_shared<ModelInput>(
            create_model_input(input_table, std::move(country), config, std::move(diseases)));

        if (!std::filesystem::exists(config.output.folder)) {
            fmt::print(fg(fmt::color::dark_salmon), "\nCreating output folder: {} ...\n",
                       config.output.folder);
            if (!std::filesystem::create_directories(config.output.folder)) {
                fmt::print(fg(fmt::color::red), "Failed to create output folder: {}\n",
                           config.output.folder);
                return exit_application(EXIT_FAILURE);
            }
        }

        auto event_bus = std::make_shared<DefaultEventBus>();
        auto result_file_writer = create_results_file_logger(config, *model_input);

        std::optional<hgps::IndividualIDTrackingWriter> individual_tracking_writer;
        if (config.output.individual_id_tracking.has_value() &&
            config.output.individual_id_tracking->enabled) {
            individual_tracking_writer.emplace(create_output_file_name(config.output, config.job_id));
        }

        auto event_monitor =
            EventMonitor{*event_bus, result_file_writer,
                         individual_tracking_writer ? &*individual_tracking_writer : nullptr};

        auto seed_generator =
            std::make_unique<hgps::MTRandom32>(resolve_effective_seed(*model_input));
        auto runner = Runner(event_bus, std::move(seed_generator));

        auto channel = SyncChannel{};

        double runtime = 0.0;
        fmt::print(fg(fmt::color::cyan), "\nStarting baseline simulation with {} trials ...\n\n",
                   config.trial_runs);
        auto baseline_sim = create_baseline_simulation(channel, factory, event_bus, model_input);

        if (config.active_intervention.has_value()) {
            fmt::print(fg(fmt::color::cyan),
                       "\nStarting intervention simulation with {} trials ...\n",
                       config.trial_runs);
            auto policy_sim = create_intervention_simulation(
                channel, factory, event_bus, model_input, config.active_intervention.value());
            runtime = runner.run(baseline_sim, policy_sim, config.trial_runs);
        } else {
            channel.close();
            runtime = runner.run(baseline_sim, config.trial_runs);
        }

        fmt::print(fg(fmt::color::light_green), "\nCompleted, elapsed time : {}ms\n\n", runtime);
        event_monitor.stop();

#ifdef CATCH_EXCEPTIONS
    } catch (const std::exception &ex) {
        fmt::print(fg(fmt::color::red), "\n\nFailed with message: {}.\n\n", ex.what());
        throw;
    }
#endif

    return exit_application(EXIT_SUCCESS);
}