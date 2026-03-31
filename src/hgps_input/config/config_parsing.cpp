#include "io/config_parsing.h"
#include "io/json_parser.h"

#include "config_section_parsing.h"
#include "config_path_parsing.h"
#include "io/json_access.h"

#include <fmt/color.h>

namespace hgps::input {

using json = nlohmann::json;

void check_version(const json &j, hgps::core::Diagnostics &diagnostics,
                   std::string_view source_path,
                   std::string_view field_path) {

    int version{};
    if (!get_to(j, "version", version, diagnostics, source_path, field_path)) {
        return;
    }

    if (version != 2) {
        diagnostics.error(
            hgps::core::DiagnosticCode::parse_failure,
            {.source_path = std::string{source_path},
             .field_path = join_field_path(field_path, "version")},
            fmt::format("Unsupported configuration schema version: {}", version));
    }
}

void load_input_info(const json &j, Configuration &config,
                     hgps::core::Diagnostics &diagnostics,
                     std::string_view source_path,
                     std::string_view field_path) {

    const auto inputs = get(j, "inputs", diagnostics, source_path, field_path);

    // Dataset
    {
        const auto dataset =
            get(inputs, "dataset", diagnostics, source_path, "inputs");

        auto file = get_file_info(dataset, config.root_path, diagnostics,
                                 source_path, "inputs.dataset");

        if (file.has_value()) {
            config.file = std::move(*file);
        }
    }

    // Settings
    {
        auto settings =
            get_settings(inputs, diagnostics, source_path, "inputs");

        if (settings.has_value()) {
            config.settings = std::move(*settings);
        }
    }
}

void load_modelling_info(const json &j, Configuration &config,
                         hgps::core::Diagnostics &diagnostics,
                         std::string_view source_path,
                         std::string_view field_path) {

    const auto modelling = get(j, "modelling", diagnostics, source_path, field_path);

    auto &info = config.modelling;

    get_to(modelling, "risk_factors", info.risk_factors,
           diagnostics, source_path, "modelling");

    // Risk factor model paths
    if (get_to(modelling, "risk_factor_models", info.risk_factor_models,
               diagnostics, source_path, "modelling")) {

        for (auto &[type, path] : info.risk_factor_models) {
            rebase_valid_path(path, config.root_path);
        }
    }

    // Baseline
    {
        auto baseline = get_baseline_info(modelling, config.root_path,
                                         diagnostics, source_path, "modelling");

        if (baseline.has_value()) {
            info.baseline_adjustment = std::move(*baseline);
        }
    }

    // SES
    {
        const auto ses_json =
            get(modelling, "ses_model", diagnostics, source_path, "modelling");

        try {
            config.ses = ses_json.get<SESInfo>();
        } catch (const std::exception &e) {
            diagnostics.error(
                hgps::core::DiagnosticCode::parse_failure,
                {.source_path = std::string{source_path},
                 .field_path = "modelling.ses_model"},
                e.what());
        }
    }

    // Optional
    get_to(modelling, "policy_start_year", info.policy_start_year,
           diagnostics, source_path, "modelling");
}

void load_running_info(const json &j, Configuration &config,
                       hgps::core::Diagnostics &diagnostics,
                       std::string_view source_path,
                       std::string_view field_path) {

    const auto running = get(j, "running", diagnostics, source_path, field_path);

    get_to(running, "start_time", config.start_time,
           diagnostics, source_path, "running");

    get_to(running, "stop_time", config.stop_time,
           diagnostics, source_path, "running");

    get_to(running, "trial_runs", config.trial_runs,
           diagnostics, source_path, "running");

    get_to(running, "sync_timeout_ms", config.sync_timeout_ms,
           diagnostics, source_path, "running");

    get_to(running, "diseases", config.diseases,
           diagnostics, source_path, "running");

    // Seed
    {
        unsigned int seed{};
        if (get_to(running, "seed", seed, diagnostics,
                   source_path, "running")) {
            config.custom_seed = seed;
        }
    }

    // Interventions
    load_interventions(running, config, diagnostics,
                       source_path, "running");
}

void load_output_info(const json &j, Configuration &config,
                      const std::optional<std::string> &output_folder,
                      hgps::core::Diagnostics &diagnostics,
                      std::string_view source_path,
                      std::string_view field_path) {

    if (!get_to(j, "output", config.output,
                diagnostics, source_path, field_path)) {
        return;
    }

    if (output_folder.has_value() != config.output.folder.empty()) {
        diagnostics.error(
            hgps::core::DiagnosticCode::parse_failure,
            {.source_path = std::string{source_path},
             .field_path = "output.folder"},
            "Specify output folder either via CLI or config, not both");
        return;
    }

    if (output_folder.has_value()) {
        config.output.folder = output_folder.value();
    } else {
        config.output.folder =
            expand_environment_variables(config.output.folder);
    }
}

} // namespace hgps::input