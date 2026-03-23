#pragma once

#include <hgps_core/datatable.h>
#include <hgps_core/string_util.h>
#include <map>

#include "hgps_input/poco.h"
#include "mapping.h"
#include "settings.h"

#include <optional>

namespace hgps {

struct RunInfo {
    unsigned int start_time{};

    unsigned int stop_time{};

    unsigned int sync_timeout_ms{};

    std::optional<unsigned int> seed{};

    core::VerboseMode verbosity{};

    unsigned int comorbidities{};

    unsigned int policy_start_year{0};
};

struct SESDefinition {
    std::string fuction_name;

    std::vector<double> parameters;
};

class ModelInput {
  public:
    ModelInput() = delete;

    ModelInput(core::DataTable &data, Settings settings, const RunInfo &run_info,
               SESDefinition ses_info, HierarchicalMapping risk_mapping,
               std::vector<core::DiseaseInfo> diseases,
               hgps::input::ProjectRequirements project_requirements,
               hgps::input::PIFInfo pif_info = hgps::input::PIFInfo{},
               std::optional<hgps::input::IndividualIdTrackingConfig>
                   individual_id_tracking_config = std::nullopt);

    const Settings &settings() const noexcept;

    const core::DataTable &data() const noexcept;

    unsigned int start_time() const noexcept;

    unsigned int stop_time() const noexcept;

    unsigned int sync_timeout_ms() const noexcept;

    const std::optional<unsigned int> &seed() const noexcept;

    const RunInfo &run() const noexcept;

    const SESDefinition &ses_definition() const noexcept;

    const HierarchicalMapping &risk_mapping() const noexcept;

    const std::vector<core::DiseaseInfo> &diseases() const noexcept;

    bool enable_income_analysis() const noexcept;

    const hgps::input::PIFInfo &population_impact_fraction() const noexcept;

    const hgps::input::ProjectRequirements &project_requirements() const noexcept;

    const std::optional<hgps::input::IndividualIdTrackingConfig> &
    individual_id_tracking_config() const noexcept;

  private:
    std::reference_wrapper<core::DataTable> input_data_;
    Settings settings_;
    RunInfo run_info_;
    SESDefinition ses_definition_;
    HierarchicalMapping risk_mapping_;
    std::vector<core::DiseaseInfo> diseases_;
    hgps::input::ProjectRequirements project_requirements_{};
    bool enable_income_analysis_{
        true}; // This is to set if results be categorised by income or not. Set to TRUE for now.
    hgps::input::PIFInfo pif_info_;
    std::optional<hgps::input::IndividualIdTrackingConfig> individual_id_tracking_config_{};
};
} // namespace hgps
