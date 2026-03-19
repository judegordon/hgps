#pragma once

#include "data_source.h"
#include "poco.h"
#include "version.h"

#include "HealthGPS.Core/api.h"
#include "HealthGPS/intervention_scenario.h"
#include "HealthGPS/modelinput.h"
#include "HealthGPS/scenario.h"
#include "HealthGPS/simulation.h"

#include <memory>
#include <optional>
#include <stdexcept>

namespace hgps::input {

struct Configuration {
    std::filesystem::path root_path;

    std::unique_ptr<DataSource> data_source;

    FileInfo file;

    SettingsInfo settings;

    SESInfo ses;

    ModellingInfo modelling;

    std::vector<std::string> diseases;

    std::optional<unsigned int> custom_seed;

    unsigned int start_time{};

    unsigned int stop_time{};

    unsigned int trial_runs{};

    unsigned int sync_timeout_ms{};

    std::optional<PolicyScenarioInfo> active_intervention;

    OutputInfo output;

    hgps::core::VerboseMode verbosity{};

    int job_id{};

    const char *app_name = PROJECT_NAME;

    const char *app_version = PROJECT_VERSION;

    std::string trend_type = "null";

    ProjectRequirements project_requirements{};

    nlohmann::json config_data;
    PIFInfo population_impact_fraction{};
};

class ConfigurationError : public std::runtime_error {
  public:
    ConfigurationError(const std::string &msg);
};

Configuration get_configuration(const std::string &config_source,
                                const std::optional<std::string> &output_folder, int job_id,
                                bool verbose);

std::vector<hgps::core::DiseaseInfo> get_diseases_info(hgps::core::Datastore &data_api,
                                                       Configuration &config);

hgps::ModelInput create_model_input(hgps::core::DataTable &input_table, hgps::core::Country country,
                                    const Configuration &config,
                                    std::vector<hgps::core::DiseaseInfo> diseases);

std::string create_output_file_name(const OutputInfo &info, int job_id);

std::unique_ptr<hgps::Scenario> create_baseline_scenario(hgps::SyncChannel &channel);

hgps::Simulation create_baseline_simulation(hgps::SyncChannel &channel,
                                            hgps::SimulationModuleFactory &factory,
                                            std::shared_ptr<const hgps::EventAggregator> event_bus,
                                            std::shared_ptr<const hgps::ModelInput> input);

std::unique_ptr<hgps::Scenario> create_intervention_scenario(hgps::SyncChannel &channel,
                                                             const PolicyScenarioInfo &info);

hgps::Simulation
create_intervention_simulation(hgps::SyncChannel &channel, hgps::SimulationModuleFactory &factory,
                               std::shared_ptr<const hgps::EventAggregator> event_bus,
                               std::shared_ptr<const hgps::ModelInput> input,
                               const PolicyScenarioInfo &info);

std::string expand_environment_variables(const std::string &path);

std::optional<unsigned int> create_job_seed(int job_id, std::optional<unsigned int> user_seed);

} // namespace hgps::input
