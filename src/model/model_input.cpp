#include "model_input.h"

#include <utility>

namespace hgps::model {

ModelInput::ModelInput(core::DataTable data, Settings settings, RunInfo run_info,
                       SesDefinition ses, HierarchicalMapping risk_mapping,
                       std::vector<core::DiseaseInfo> diseases,
                       config::ProjectRequirements requirements,
                       std::optional<config::IndividualTracking> tracking)
    : data_{std::move(data)}, settings_{std::move(settings)}, run_info_{run_info},
      ses_{std::move(ses)}, risk_mapping_{std::move(risk_mapping)},
      diseases_{std::move(diseases)}, requirements_{std::move(requirements)},
      income_layout_{core::income_category_layout_from_config(requirements_.income.categories)},
      tracking_{std::move(tracking)} {}

bool ModelInput::income_analysis_enabled() const noexcept {
    // The baseline hard-codes this to true with a comment saying "for now"; here it follows the
    // project requirement that decides whether the income-stratified files are written at all.
    return requirements_.income.enabled && requirements_.income.income_based_csv_output;
}

} // namespace hgps::model
