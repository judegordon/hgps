#pragma once

#include "HealthGPS.Core/array2d.h"
#include "interfaces.h"
#include "mapping.h"

namespace hgps {

struct Coefficient {
    double value{};

    double pvalue{};

    double tvalue{};

    double std_error{};
};

struct LinearModel {
    std::unordered_map<core::Identifier, Coefficient> coefficients;

    double residuals_standard_deviation{};

    double rsquared{};
};

struct HierarchicalLevel {

    std::unordered_map<core::Identifier, int> variables;

    core::DoubleArray2D transition;

    core::DoubleArray2D inverse_transition;

    core::DoubleArray2D residual_distribution;

    core::DoubleArray2D correlation;

    std::vector<double> variances;
};

class StaticHierarchicalLinearModel final : public RiskFactorModel {
  public:
    StaticHierarchicalLinearModel(const std::unordered_map<core::Identifier, LinearModel> &models,
                                  const std::map<int, HierarchicalLevel> &levels);

    RiskFactorModelType type() const noexcept override;

    std::string name() const noexcept override;

    void generate_risk_factors(RuntimeContext &context) override;

    void update_risk_factors(RuntimeContext &context) override;

  private:
    const std::unordered_map<core::Identifier, LinearModel> &models_;
    const std::map<int, HierarchicalLevel> &levels_;

    void generate_for_entity(RuntimeContext &context, Person &entity, int level,
                             std::vector<MappingEntry> &level_factors);
};

class StaticHierarchicalLinearModelDefinition final : public RiskFactorModelDefinition {
  public:
    StaticHierarchicalLinearModelDefinition(
        std::unordered_map<core::Identifier, LinearModel> linear_models,
        std::map<int, HierarchicalLevel> model_levels);

    std::unique_ptr<RiskFactorModel> create_model() const override;

  private:
    std::unordered_map<core::Identifier, LinearModel> models_;
    std::map<int, HierarchicalLevel> levels_;
};

} // namespace hgps
