#pragma once

#include "hgps_input/config/config_types.h"
#include "analysis/analysis_definition.h"
#include "types/interfaces.h"
#include "data/model_input.h"
#include "data/repository.h"
#include "runtime_context.h"
#include "models/weight_model.h"

#include <optional>
#include <vector>

namespace hgps {

class AnalysisModule final : public UpdatableModule {
  public:
    AnalysisModule() = delete;

    AnalysisModule(AnalysisDefinition &&definition, WeightModel &&classifier,
                   core::IntegerInterval age_range, unsigned int comorbidities);

    SimulationModuleType type() const noexcept override;
    const std::string &name() const noexcept override;

    void initialise_population(RuntimeContext &context) override;
    void update_population(RuntimeContext &context) override;

    void set_income_analysis_enabled(bool enabled) noexcept;
    void set_individual_id_tracking_config(
        std::optional<hgps::input::IndividualIdTrackingConfig> config) noexcept;

  private:
    AnalysisDefinition definition_;
    WeightModel weight_classifier_;
    DoubleAgeGenderTable residual_disability_weight_;
    std::vector<std::string> channels_;
    unsigned int comorbidities_;
    std::string name_{"Analysis"};
    bool enable_income_analysis_{true};
    std::optional<hgps::input::IndividualIdTrackingConfig> individual_id_tracking_config_{};

    double calculate_residual_disability_weight(int age, core::Gender gender,
                                                const DoubleAgeGenderTable &expected_sum,
                                                const IntegerAgeGenderTable &expected_count);

    void publish_result_message(RuntimeContext &context) const;
};

std::unique_ptr<AnalysisModule> build_analysis_module(Repository &repository,
                                                      const ModelInput &config);

} // namespace hgps