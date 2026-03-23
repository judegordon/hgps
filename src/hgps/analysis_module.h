#pragma once

#include "hgps_input/poco.h"
#include "analysis_definition.h"
#include "interfaces.h"
#include "modelinput.h"
#include "repository.h"
#include "result_message.h"
#include "runtime_context.h"
#include "weight_model.h"

#include <optional>

namespace hgps {
class AnalysisModule final : public UpdatableModule {
  public:
    AnalysisModule() = delete;
    AnalysisModule(AnalysisDefinition &&definition, WeightModel &&classifier,
                   const core::IntegerInterval age_range, unsigned int comorbidities);
    AnalysisModule(AnalysisDefinition &&definition, WeightModel &&classifier,
                   core::IntegerInterval age_range, unsigned int comorbidities,
                   std::vector<double> calculated_factors);

    SimulationModuleType type() const noexcept override;

    const std::string &name() const noexcept override;

    void initialise_population(RuntimeContext &context) override;

    void update_population(RuntimeContext &context) override;

    void set_income_analysis_enabled(bool enabled) noexcept;

    void set_individual_id_tracking_config(
        std::optional<hgps::input::IndividualIdTrackingConfig> config) noexcept;

    static std::vector<core::Income> get_available_income_categories(RuntimeContext &context);

    static std::string income_category_to_string(core::Income income);

  private:
    AnalysisDefinition definition_;
    WeightModel weight_classifier_;
    DoubleAgeGenderTable residual_disability_weight_;
    std::vector<std::string> channels_;
    unsigned int comorbidities_;
    std::string name_{"Analysis"};
    std::vector<core::Identifier> factors_to_calculate_ = {"Gender"_id, "Age"_id};
    std::vector<double> calculated_stats_;
    std::vector<size_t> factor_bins_;
    std::vector<double> factor_bin_widths_;
    std::vector<double> factor_min_values_;
    bool enable_income_analysis_{
        true}; // This is to set if results be categorised by income or not. Set to TRUE for now.
    std::optional<hgps::input::IndividualIdTrackingConfig> individual_id_tracking_config_{};

    void initialise_vector(RuntimeContext &context);

    double calculate_residual_disability_weight(int age, core::Gender gender,
                                                const DoubleAgeGenderTable &expected_sum,
                                                const IntegerAgeGenderTable &expected_count);

    void publish_result_message(RuntimeContext &context) const;
    void publish_individual_tracking_if_enabled(RuntimeContext &context) const;
    void calculate_historical_statistics(RuntimeContext &context, ModelResult &result) const;
    void calculate_income_based_statistics(RuntimeContext &context, ModelResult &result) const;
    double calculate_disability_weight(const Person &entity) const;
    DALYsIndicator calculate_dalys(Population &population, unsigned int max_age,
                                   unsigned int death_year) const;

    void calculate_population_statistics(RuntimeContext &context);
    void calculate_population_statistics(RuntimeContext &context, DataSeries &series) const;
    void calculate_income_based_population_statistics(RuntimeContext &context,
                                                      DataSeries &series) const;

    void calculate_income_based_standard_deviation(RuntimeContext &context,
                                                   DataSeries &series) const;

    void classify_weight(hgps::DataSeries &series, const hgps::Person &entity) const;
    void initialise_output_channels(RuntimeContext &context);
    void initialise_income_output_channels(RuntimeContext &context) const;

    void calculate_standard_deviation(RuntimeContext &context, DataSeries &series) const;
};

std::unique_ptr<AnalysisModule> build_analysis_module(Repository &repository,
                                                      const ModelInput &config);
} // namespace hgps
