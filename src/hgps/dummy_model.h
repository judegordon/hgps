#pragma once

#include "risk_factor_model.h"

#include <string>
#include <vector>

namespace hgps {

class DummyModel final : public RiskFactorModel {
  public:
    DummyModel(RiskFactorModelType type, const std::vector<core::Identifier> &names,
               const std::vector<double> &values, const std::vector<double> &policy,
               const std::vector<int> &policy_start);

    RiskFactorModelType type() const noexcept override;

    std::string name() const noexcept override;

    void generate_risk_factors(RuntimeContext &context) override;

    void update_risk_factors(RuntimeContext &context) override;

  private:
    void set_risk_factors(Person &person, ScenarioType scenario, int time_elapsed) const;

    const RiskFactorModelType type_;
    const std::vector<core::Identifier> &names_;
    const std::vector<double> &values_;
    const std::vector<double> &policy_;
    const std::vector<int> &policy_start_;
};

class DummyModelDefinition final : public RiskFactorModelDefinition {
  public:
    DummyModelDefinition(RiskFactorModelType type, std::vector<core::Identifier> names,
                         std::vector<double> values, std::vector<double> policy,
                         std::vector<int> policy_start);

    std::unique_ptr<RiskFactorModel> create_model() const override;

  private:
    RiskFactorModelType type_;
    std::vector<core::Identifier> names_;
    std::vector<double> values_;
    std::vector<double> policy_;
    std::vector<int> policy_start_;
};

} // namespace hgps
