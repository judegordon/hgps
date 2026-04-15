#pragma once

#include "hgps_core/forward_type.h"
#include "hgps_core/types/identifier.h"
#include "disease.h"
#include "gender_table.h"
#include "gender_value.h"
#include "interfaces.h"
#include "life_table.h"
#include "modelinput.h"
#include "repository.h"
#include "runtime_context.h"

namespace hgps {

class DemographicModule final : public SimulationModule {
  public:
    DemographicModule() = delete;

    DemographicModule(std::map<int, std::map<int, PopulationRecord>> &&pop_data,
                      LifeTable &&life_table);

    SimulationModuleType type() const noexcept override;

    const std::string &name() const noexcept override;

    std::size_t get_total_population_size(int time_year) const noexcept;

    const std::map<int, PopulationRecord> &get_population_distribution(int time_year) const;

    void initialise_population(RuntimeContext &context) override;

    void update_population(RuntimeContext &context, const DiseaseModule &disease_host);

    void set_region_prevalence(
        const std::map<core::Identifier, std::map<core::Gender, std::map<std::string, double>>>
            &region_data);

    void set_ethnicity_prevalence(
        const std::map<core::Identifier,
                       std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>>
            &ethnicity_data);

  private:
    std::map<int, std::map<int, PopulationRecord>> pop_data_;
    LifeTable life_table_;
    GenderTable<int, double> birth_rates_;
    GenderTable<int, double> residual_death_rates_;

    std::map<core::Identifier, std::map<core::Gender, std::map<std::string, double>>>
        region_prevalence_;

    std::map<core::Identifier,
             std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>>
        ethnicity_prevalence_;

    std::string name_{"Demographic"};

    void initialise_birth_rates();

    void initialise_region(RuntimeContext &context, Person &person, Random &random);

    void initialise_ethnicity(RuntimeContext &context, Person &person, Random &random);

    double get_total_deaths(int time_year) const noexcept;
    std::map<int, DoubleGenderValue> get_age_gender_distribution(int time_year) const noexcept;
    DoubleGenderValue get_birth_rate(int time_year) const noexcept;
    double get_residual_death_rate(int age, core::Gender gender) const noexcept;

    GenderTable<int, double> create_death_rates_table(int time_year);
    GenderTable<int, double> calculate_residual_mortality(RuntimeContext &context,
                                                          const DiseaseModule &disease_host);

    void update_residual_mortality(RuntimeContext &context, const DiseaseModule &disease_host);
    static double calculate_excess_mortality_product(const Person &entity,
                                                     const DiseaseModule &disease_host);
    int update_age_and_death_events(RuntimeContext &context, const DiseaseModule &disease_host);
};

std::unique_ptr<DemographicModule> build_population_module(Repository &repository,
                                                           const ModelInput &config);

} // namespace hgps
