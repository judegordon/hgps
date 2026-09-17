// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/demographic.h, demographic.cpp.
#pragma once

#include "containers.h"
#include "core/identifier.h"
#include "life_table.h"
#include "module.h"
#include "sim/scenario.h"

#include <map>
#include <memory>
#include <string>

namespace hgps::model {

/// @brief Region shares by age and sex: age key ("age_0", "age_1", …) to sex to region name.
using RegionPrevalence =
    std::map<core::Identifier, std::map<core::Gender, std::map<std::string, double>>>;

/// @brief Ethnicity shares by age group, sex and region.
using EthnicityPrevalence =
    std::map<core::Identifier,
             std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>>;

/// @brief Births, deaths, ageing, net migration, and the region and ethnicity of each person.
///
/// Three differences from the baseline, all recorded in docs/deviations.md:
///
///  - the residual-mortality reduction over the population uses
///    `core::parallel::reduce_ordered`, so its result does not depend on the thread count. The
///    baseline accumulates into a shared table under a mutex (audit N-7), and the earlier rewrite
///    left this one site parallel after removing the pattern elsewhere (finding R-04);
///  - region and ethnicity are drawn through `rng::Categorical`, which can only be built from an
///    ordered sequence (determinism clause D4);
///  - the residual-mortality table the baseline scenario computes travels to the intervention
///    scenario in the journal rather than over a channel with a timeout (ADR 0009).
class DemographicModule final : public SimulationModule {
  public:
    DemographicModule() = delete;

    /// @throws std::invalid_argument if the population data and the life table disagree about
    ///         their year or age limits — which would silently produce zero birth or death rates
    ///         for the years they do not share.
    DemographicModule(std::map<int, std::map<int, PopulationRecord>> population_data,
                      LifeTable life_table);

    ModuleType type() const noexcept override { return ModuleType::Demographic; }
    const std::string &name() const noexcept override { return name_; }

    /// @brief The real population in a year, as the data gives it.
    std::size_t get_total_population_size(int time_year) const noexcept;

    /// @throws std::out_of_range for a year the data does not cover.
    const std::map<int, PopulationRecord> &get_population_distribution(int time_year) const;

    /// @brief Assigns age, sex, region and ethnicity to the initial cohort.
    void initialise_population(RuntimeContext &context) override;

    /// @brief One year: residual mortality, deaths, ageing, births.
    ///
    /// Migration is applied by the engine, which owns the journal.
    void update_population(RuntimeContext &context, const ExcessMortalityHost &disease_host,
                           sim::ScenarioJournal &journal);

    void set_region_prevalence(RegionPrevalence region_data);
    void set_ethnicity_prevalence(EthnicityPrevalence ethnicity_data);

    /// @brief The residual mortality rate for an age and sex, or 0 before it is computed.
    double get_residual_death_rate(int age, core::Gender gender) const noexcept;

    /// @brief The birth rate per person of the previous year's population, by sex.
    GenderValue<double> get_birth_rate(int time_year) const noexcept;

    /// @brief The share of the population at each age, by sex, in a year.
    std::map<int, GenderValue<double>> get_age_gender_distribution(int time_year) const;

  private:
    std::map<int, std::map<int, PopulationRecord>> population_data_;
    LifeTable life_table_;
    GenderTable<int, double> birth_rates_;
    GenderTable<int, double> residual_death_rates_;
    RegionPrevalence region_prevalence_;
    EthnicityPrevalence ethnicity_prevalence_;
    std::string name_{"Demographic"};

    void initialise_birth_rates();

    void initialise_region(RuntimeContext &context, Person &person) const;
    void initialise_ethnicity(RuntimeContext &context, Person &person) const;

    double get_total_deaths(int time_year) const noexcept;

    GenderTable<int, double> create_death_rates_table(int time_year) const;

    /// @brief The mortality left over once modelled disease mortality is accounted for.
    GenderTable<int, double> calculate_residual_mortality(
        RuntimeContext &context, const ExcessMortalityHost &disease_host) const;

    void update_residual_mortality(RuntimeContext &context,
                                   const ExcessMortalityHost &disease_host,
                                   sim::ScenarioJournal &journal);

    static double calculate_excess_mortality_product(const Person &person,
                                                     const ExcessMortalityHost &disease_host);

    int update_age_and_death_events(RuntimeContext &context,
                                    const ExcessMortalityHost &disease_host) const;
};

} // namespace hgps::model
