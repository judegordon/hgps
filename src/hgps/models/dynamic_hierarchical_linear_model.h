#pragma once

#include "data/mapping.h"
#include "hgps_core/types/identifier.h"
#include "risk_factor_adjustable_model.h"
#include "utils/random_algorithm.h"

namespace hgps {

struct FactorDynamicEquation {
    std::string name;
    std::map<core::Identifier, double> coefficients{};
    double residuals_standard_deviation{};
};

struct AgeGroupGenderEquation {
    core::IntegerInterval age_group;
    std::map<core::Identifier, FactorDynamicEquation> male{};
    std::map<core::Identifier, FactorDynamicEquation> female{};
};

class DynamicHierarchicalLinearModel final : public RiskFactorAdjustableModel {
  public:
    DynamicHierarchicalLinearModel(
        std::shared_ptr<RiskFactorSexAgeTable> expected,
        std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
        std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
        const std::map<core::IntegerInterval, AgeGroupGenderEquation> &equations,
        const std::map<core::Identifier, core::Identifier> &variables,
        double boundary_percentage);

    RiskFactorModelType type() const noexcept override;

    std::string name() const override;

    void generate_risk_factors(RuntimeContext &context) override;

    void update_risk_factors(RuntimeContext &context) override;

  private:
    const std::map<core::IntegerInterval, AgeGroupGenderEquation> &equations_;
    const std::map<core::Identifier, core::Identifier> &variables_;
    double boundary_percentage_;

    const AgeGroupGenderEquation &equations_at(int age) const;

    void update_risk_factors_exposure(
        RuntimeContext &context, Person &entity,
        const std::map<core::Identifier, double> &current_risk_factors,
        const std::map<core::Identifier, FactorDynamicEquation> &equations);

    static std::map<core::Identifier, double>
    get_current_risk_factors(const HierarchicalMapping &mapping, const Person &entity);

    double sample_normal_with_boundary(Random &random, double mean, double standard_deviation,
                                       double boundary) const;
};

class DynamicHierarchicalLinearModelDefinition : public RiskFactorAdjustableModelDefinition {
  public:
    DynamicHierarchicalLinearModelDefinition(
        std::unique_ptr<RiskFactorSexAgeTable> expected,
        std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
        std::unique_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
        std::map<core::IntegerInterval, AgeGroupGenderEquation> equations,
        std::map<core::Identifier, core::Identifier> variables,
        double boundary_percentage = 0.05);

    std::unique_ptr<RiskFactorModel> create_model() const override;

  private:
    std::map<core::IntegerInterval, AgeGroupGenderEquation> equations_;
    std::map<core::Identifier, core::Identifier> variables_;
    double boundary_percentage_;
};

} // namespace hgps