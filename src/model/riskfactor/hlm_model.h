// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/static_hierarchical_linear_model.{h,cpp},
//         dynamic_hierarchical_linear_model.{h,cpp}.
#pragma once

#include "core/array2d.h"
#include "core/identifier.h"
#include "core/interval.h"
#include "model/mapping.h"
#include "risk_factor_model.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief One fitted coefficient, with the diagnostics the model files carry.
struct Coefficient {
    double value{};
    double pvalue{};
    double tvalue{};
    double std_error{};
};

/// @brief One factor's fitted regression.
struct LinearEquation {
    /// @brief Ordered by predictor name, so the sum over the terms has a stated order. The
    ///        baseline uses an unordered_map here and sums over it — see docs/deviations.md.
    std::map<core::Identifier, Coefficient> coefficients;

    double residuals_standard_deviation{};
    double rsquared{};
};

/// @brief One level of the static hierarchical model.
struct HierarchicalLevel {
    /// @brief Each factor's column index in the matrices below.
    std::map<core::Identifier, std::size_t> variables;

    /// @brief The transition matrix applied to the sampled residuals.
    core::DoubleArray2D transition;

    core::DoubleArray2D inverse_transition;

    /// @brief The empirical residual rows the model samples from.
    core::DoubleArray2D residual_distribution;

    core::DoubleArray2D correlation;

    std::vector<double> variances;
};

/// @brief The `HLM` static model: a fitted regression per factor per level, plus correlated
///        residuals sampled from an empirical distribution.
class StaticHierarchicalLinearModel final : public RiskFactorModel {
  public:
    StaticHierarchicalLinearModel(
        std::shared_ptr<const std::map<core::Identifier, LinearEquation>> models,
        std::shared_ptr<const std::map<int, HierarchicalLevel>> levels);

    RiskFactorModelType type() const noexcept override { return RiskFactorModelType::Static; }
    std::string name() const noexcept override { return "Static"; }

    /// @brief Generates every person's factors, level by level.
    void generate_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) override;

    /// @brief Generates newborns' factors; everyone else is the dynamic model's business.
    void update_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) override;

  private:
    std::shared_ptr<const std::map<core::Identifier, LinearEquation>> models_;
    std::shared_ptr<const std::map<int, HierarchicalLevel>> levels_;

    void generate_for_person(RuntimeContext &context, Person &person, int level,
                             const std::vector<MappingEntry> &level_factors) const;
};

/// @brief One factor's year-on-year equation.
struct FactorDynamicEquation {
    std::string name;
    std::map<core::Identifier, double> coefficients;
    double residuals_standard_deviation{};
};

/// @brief The equations for one age band, per sex.
struct AgeGroupGenderEquation {
    core::IntegerInterval age_group;
    std::map<core::Identifier, FactorDynamicEquation> male;
    std::map<core::Identifier, FactorDynamicEquation> female;
};

/// @brief The `EBHLM` dynamic model: a per-age-band, per-sex regression on last year's values,
///        with a bounded normal residual, then calibration to the FactorsMean tables.
class DynamicHierarchicalLinearModel final : public AdjustableRiskFactorModel {
  public:
    DynamicHierarchicalLinearModel(
        std::shared_ptr<const SexAgeFactorTable> expected,
        std::shared_ptr<const std::map<core::Identifier, double>> trend,
        std::shared_ptr<const std::map<core::Identifier, int>> trend_steps,
        std::shared_ptr<const std::map<core::IntegerInterval, AgeGroupGenderEquation>> equations,
        std::shared_ptr<const std::map<core::Identifier, core::Identifier>> variables,
        double boundary_percentage);

    RiskFactorModelType type() const noexcept override { return RiskFactorModelType::Dynamic; }
    std::string name() const noexcept override { return "Dynamic"; }

    /// @brief Calibrates the generated factors to the expected means; generates nothing itself.
    void generate_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) override;

    /// @brief Moves every non-newborn one year, then calibrates.
    void update_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) override;

    /// @brief True. `update_exposure` is the one place in this build that calls `Scenario::apply`,
    ///        which is also true of the baseline (`dynamic_hierarchical_linear_model.cpp:110`).
    bool applies_the_active_scenario() const noexcept override { return true; }

  private:
    std::shared_ptr<const std::map<core::IntegerInterval, AgeGroupGenderEquation>> equations_;
    std::shared_ptr<const std::map<core::Identifier, core::Identifier>> variables_;
    double boundary_percentage_;
    std::vector<core::Identifier> factor_keys_;

    /// @brief The equations for an age: the band that contains it, or the nearest band's.
    const AgeGroupGenderEquation &equations_at(int age) const;

    void update_exposure(RuntimeContext &context, Person &person,
                         const std::map<core::Identifier, double> &current,
                         const std::map<core::Identifier, FactorDynamicEquation> &equations) const;

    static std::map<core::Identifier, double>
    current_risk_factors(const HierarchicalMapping &mapping, const Person &person);

    /// @brief A normal draw capped at ±(boundary_percentage × value), so one year cannot move a
    ///        factor by an implausible amount.
    double sample_normal_with_boundary(rng::RandomSource &random, double mean,
                                        double standard_deviation, double boundary) const;
};

} // namespace hgps::model
