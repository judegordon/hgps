// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{modelinput,settings,interfaces}.h.
#pragma once

#include "config/types.h"
#include "core/datatable.h"
#include "core/entities.h"
#include "core/income_category_layout.h"
#include "core/interval.h"
#include "mapping.h"

#include <optional>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief The population the experiment draws, resolved against the data store.
struct Settings {
    core::Country country;

    /// @brief The fraction of the real population simulated.
    double size_fraction{};

    core::IntegerInterval age_range{};
};

/// @brief How the run is executed.
struct RunInfo {
    unsigned int start_time{};
    unsigned int stop_time{};

    /// @brief Required. There is no unseeded run (determinism clause D1).
    std::uint32_t seed{};

    core::VerboseMode verbosity{core::VerboseMode::none};
    unsigned int comorbidities{};

    /// @brief The calendar year policies start. 0 means start_time + 2, as upstream.
    unsigned int policy_start_year{0};

    unsigned int trial_runs{1};
};

/// @brief The socio-economic status noise model's parameters.
struct SesDefinition {
    std::string function_name;
    std::vector<double> parameters;
};

/// @brief Everything the modules need that does not change during a run.
///
/// Immutable once built, and shared by both scenarios of a trial run — which is safe precisely
/// because it is immutable and because scenarios run one after the other.
class ModelInput {
  public:
    ModelInput() = delete;

    ModelInput(core::DataTable data, Settings settings, RunInfo run_info, SesDefinition ses,
               HierarchicalMapping risk_mapping, std::vector<core::DiseaseInfo> diseases,
               config::ProjectRequirements requirements,
               std::optional<config::IndividualTracking> tracking = std::nullopt);

    const Settings &settings() const noexcept { return settings_; }
    const core::DataTable &data() const noexcept { return data_; }

    unsigned int start_time() const noexcept { return run_info_.start_time; }
    unsigned int stop_time() const noexcept { return run_info_.stop_time; }
    std::uint32_t seed() const noexcept { return run_info_.seed; }
    const RunInfo &run() const noexcept { return run_info_; }

    const SesDefinition &ses_definition() const noexcept { return ses_; }
    const HierarchicalMapping &risk_mapping() const noexcept { return risk_mapping_; }
    const std::vector<core::DiseaseInfo> &diseases() const noexcept { return diseases_; }

    const config::ProjectRequirements &project_requirements() const noexcept {
        return requirements_;
    }

    /// @brief The ordered income categories this project uses, derived once from
    ///        project_requirements.income.categories.
    const core::IncomeCategoryLayout &income_layout() const noexcept { return income_layout_; }

    /// @brief Whether results are also reported by income category.
    bool income_analysis_enabled() const noexcept;

    const std::optional<config::IndividualTracking> &individual_tracking() const noexcept {
        return tracking_;
    }

  private:
    core::DataTable data_;
    Settings settings_;
    RunInfo run_info_;
    SesDefinition ses_;
    HierarchicalMapping risk_mapping_;
    std::vector<core::DiseaseInfo> diseases_;
    config::ProjectRequirements requirements_;
    core::IncomeCategoryLayout income_layout_;
    std::optional<config::IndividualTracking> tracking_;
};

} // namespace hgps::model
