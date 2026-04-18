#pragma once

#include "model_input.h"
#include "repository.h"

namespace hgps {

class RiskFactorModule final : public RiskFactorHostModule {
  public:
    RiskFactorModule() = delete;

    RiskFactorModule(std::map<RiskFactorModelType, std::unique_ptr<RiskFactorModel>> models);

    SimulationModuleType type() const noexcept override;

    const std::string &name() const noexcept override;

    std::size_t size() const noexcept override;

    bool contains(const RiskFactorModelType &model_type) const noexcept override;

    RiskFactorModel &at(const RiskFactorModelType &model_type) const;

    void initialise_population(RuntimeContext &context) override;

    void update_population(RuntimeContext &context) override;

  private:
    std::map<RiskFactorModelType, std::unique_ptr<RiskFactorModel>> models_;
    std::string name_{"RiskFactor"};
};

std::unique_ptr<RiskFactorModule> build_risk_factor_module(Repository &repository,
                                                           const ModelInput &config);

} // namespace hgps
