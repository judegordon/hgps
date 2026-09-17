// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/analysis_module.{h,cpp} (2,202 lines) and analysis_definition.h,
//         split here into module.cpp, channels.cpp, burden.cpp, series.cpp and income_strata.cpp
//         (docs/decisions/0019-split-the-monolith-translation-units.md).
#pragma once

#include "core/identifier.h"
#include "core/interval.h"
#include "model/containers.h"
#include "model/module.h"
#include "model/riskfactor/risk_factor_model.h"
#include "model/results.h"
#include "model/weight_model.h"

#include <map>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief One DALY unit. Results are reported per 100,000 person-years.
inline constexpr double kDalyUnits = 100'000.0;

/// @brief The burden-of-disease inputs: life expectancy, observed YLD, disability weights.
class AnalysisDefinition {
  public:
    AnalysisDefinition(GenderTable<int, float> life_expectancy, DoubleAgeGenderTable observed_yld,
                       std::map<core::Identifier, float> disability_weights);

    const GenderTable<int, float> &life_expectancy() const noexcept { return life_expectancy_; }
    const DoubleAgeGenderTable &observed_yld() const noexcept { return observed_yld_; }
    const std::map<core::Identifier, float> &disability_weights() const noexcept {
        return disability_weights_;
    }

  private:
    GenderTable<int, float> life_expectancy_;
    DoubleAgeGenderTable observed_yld_;
    std::map<core::Identifier, float> disability_weights_;
};

/// @brief Computes everything the results files report.
///
/// Differs from the baseline in one structural way: the result of a year's analysis is **returned**
/// rather than published onto a concurrent queue. That is what gives the output a defined row
/// order (audit B-01, ADR 0020) — and it is also why this module does not need the event bus.
class AnalysisModule final : public UpdatableModule {
  public:
    AnalysisModule() = delete;

    AnalysisModule(AnalysisDefinition definition, WeightModel classifier,
                   core::IntegerInterval age_range, unsigned int comorbidities);

    ModuleType type() const noexcept override { return ModuleType::Analysis; }
    const std::string &name() const noexcept override { return name_; }

    /// @brief Computes the residual disability weights and the output channel list.
    void initialise_population(RuntimeContext &context) override;

    /// @brief Required by the module interface; a year's analysis is produced by analyse().
    void update_population(RuntimeContext &context) override;

    /// @brief One year's results.
    ///
    /// @param context The world as it stands after every other module has run.
    /// @return The year's aggregate results and per-age series.
    ModelResult analyse(RuntimeContext &context) const;

    /// @brief Whether results are also reported by income category.
    void set_income_analysis_enabled(bool enabled) noexcept { income_analysis_ = enabled; }

    /// @brief What the loaded risk-factor models assign, which decides several output channels.
    void set_assigned_attributes(AssignedAttributes assigned) noexcept {
        assigned_ = assigned;
    }

    /// @brief The output channels, in the order the result file's columns follow.
    const std::vector<std::string> &channels() const noexcept { return channels_; }

  private:
    AnalysisDefinition definition_;
    WeightModel classifier_;
    AssignedAttributes assigned_{};
    DoubleAgeGenderTable residual_disability_weight_;
    std::vector<std::string> channels_;
    unsigned int comorbidities_;
    bool income_analysis_{true};
    std::string name_{"Analysis"};

    // --- channels.cpp
    void initialise_output_channels(RuntimeContext &context);

    // --- burden.cpp
    double calculate_residual_disability_weight(int age, core::Gender gender,
                                                const DoubleAgeGenderTable &expected_sum,
                                                const IntegerAgeGenderTable &expected_count) const;

    /// @brief The combined disability weight of a person's active diseases, plus the residual.
    double calculate_disability_weight(const Person &person) const;

    DALYsIndicator calculate_dalys(const Population &population, unsigned int max_age,
                                   unsigned int death_year) const;

    // --- module.cpp
    void calculate_historical_statistics(RuntimeContext &context, ModelResult &result) const;

    // --- series.cpp
    void calculate_population_statistics(RuntimeContext &context, DataSeries &series) const;
    void calculate_standard_deviation(RuntimeContext &context, DataSeries &series) const;
    void classify_weight(DataSeries &series, const Person &person) const;

    // --- income_strata.cpp
    void calculate_income_based_statistics(RuntimeContext &context, ModelResult &result) const;
    void calculate_income_based_series(RuntimeContext &context, DataSeries &series) const;
};

} // namespace hgps::model
