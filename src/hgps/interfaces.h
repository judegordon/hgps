#pragma once

#include "hgps_core/forward_type.h"
#include "hgps_core/types/identifier.h"

#include "person.h"
#include "risk_factor_model.h"
#include "runtime_context.h"

namespace hgps {

enum class SimulationModuleType : uint8_t {
    RiskFactor,

    SES,

    Demographic,

    Disease,

    Analysis,
};

class SimulationModule {
  public:
    virtual ~SimulationModule() = default;

    virtual SimulationModuleType type() const noexcept = 0;

    virtual const std::string &name() const noexcept = 0;

    virtual void initialise_population(RuntimeContext &context) = 0;
};

class UpdatableModule : public SimulationModule {
  public:
    virtual void update_population(RuntimeContext &context) = 0;
};

class RiskFactorHostModule : public UpdatableModule {
  public:
    virtual std::size_t size() const noexcept = 0;

    virtual bool contains(const RiskFactorModelType &modelType) const noexcept = 0;
};

struct PopulationRecord {
    PopulationRecord(int pop_age, float num_males, float num_females)
        : age{pop_age}, males{num_males}, females{num_females} {}

    int age{};

    float males{};

    float females{};

    float total() const noexcept { return males + females; }
};

class DiseaseModel {
  public:
    virtual ~DiseaseModel() = default;

    virtual core::DiseaseGroup group() const noexcept = 0;

    virtual const core::Identifier &disease_type() const noexcept = 0;

    virtual void initialise_disease_status(RuntimeContext &context) = 0;

    virtual void initialise_average_relative_risk(RuntimeContext &context) = 0;

    virtual void update_disease_status(RuntimeContext &context) = 0;

    virtual double get_excess_mortality(const Person &entity) const noexcept = 0;
};

} // namespace hgps
