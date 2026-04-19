#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "simulation/runtime_context.h"

namespace hgps {

enum class RiskFactorModelType : uint8_t {
    Static,
    Dynamic,
};

class RiskFactorModel {
  public:
    virtual ~RiskFactorModel() = default;

    virtual RiskFactorModelType type() const noexcept = 0;

    virtual std::string name() const = 0;

    virtual void generate_risk_factors(RuntimeContext &context) = 0;

    virtual void update_risk_factors(RuntimeContext &context) = 0;
};

class RiskFactorModelDefinition {
  public:
    virtual ~RiskFactorModelDefinition() = default;

    virtual std::unique_ptr<RiskFactorModel> create_model() const = 0;
};

} // namespace hgps