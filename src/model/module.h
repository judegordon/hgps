// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/interfaces.h.
#pragma once

#include "core/identifier.h"
#include "person.h"
#include "runtime_context.h"

#include <cstdint>
#include <string>

namespace hgps::model {

/// @brief The five module slots, in the order they run.
///
/// The order is fixed in code, not configured, and it is load-bearing: the single random stream is
/// consumed in this order, so changing it changes every result. The baseline says as much in a
/// comment (`/* Note: order is very important */`) without saying what breaks.
enum class ModuleType : std::uint8_t {
    Demographic,
    SES,
    RiskFactor,
    Disease,
    Analysis,
};

/// @brief Something that takes part in a simulation run.
class SimulationModule {
  public:
    SimulationModule() = default;
    virtual ~SimulationModule() = default;
    SimulationModule(const SimulationModule &) = delete;
    SimulationModule &operator=(const SimulationModule &) = delete;
    SimulationModule(SimulationModule &&) = delete;
    SimulationModule &operator=(SimulationModule &&) = delete;

    virtual ModuleType type() const noexcept = 0;
    virtual const std::string &name() const noexcept = 0;

    /// @brief Sets the module's state for the initial cohort.
    virtual void initialise_population(RuntimeContext &context) = 0;
};

/// @brief A module that also runs once per simulated year.
class UpdatableModule : public SimulationModule {
  public:
    virtual void update_population(RuntimeContext &context) = 0;
};

/// @brief One age's population, as the demographic data gives it.
struct PopulationRecord {
    PopulationRecord() = default;
    PopulationRecord(int population_age, float num_males, float num_females)
        : age{population_age}, males{num_males}, females{num_females} {}

    int age{};
    float males{};
    float females{};

    float total() const noexcept { return males + females; }
};

/// @brief What the demographic module needs from the disease module.
///
/// An interface rather than a concrete dependency, because the two modules would otherwise be
/// circular: mortality depends on disease status, and disease incidence depends on who is alive.
class ExcessMortalityHost {
  public:
    ExcessMortalityHost() = default;
    virtual ~ExcessMortalityHost() = default;
    ExcessMortalityHost(const ExcessMortalityHost &) = delete;
    ExcessMortalityHost &operator=(const ExcessMortalityHost &) = delete;
    ExcessMortalityHost(ExcessMortalityHost &&) = delete;
    ExcessMortalityHost &operator=(ExcessMortalityHost &&) = delete;

    /// @brief The excess mortality a disease adds for a person, or 0 if the disease is not
    ///        modelled.
    virtual double excess_mortality(const core::Identifier &disease,
                                    const Person &person) const = 0;
};

} // namespace hgps::model
