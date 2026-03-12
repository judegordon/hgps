#pragma once

#include "HealthGPS.Core/univariate_summary.h"

#include "event_aggregator.h"
#include "gender_table.h"
#include "runtime_context.h"
#include "simulation_module.h"

#include <adevs/adevs.h>
#include <vector>

namespace hgps {

class Simulation : public adevs::Model<int> {
  public:
    Simulation() = delete;

    explicit Simulation(SimulationModuleFactory &factory,
                        std::shared_ptr<const EventAggregator> bus,
                        std::shared_ptr<const ModelInput> inputs,
                        std::unique_ptr<Scenario> scenario);

    virtual ~Simulation() = default;

    adevs::Time init(adevs::SimEnv<int> *env) override;

    adevs::Time update(adevs::SimEnv<int> *env) override;

    adevs::Time update(adevs::SimEnv<int> *env, std::vector<int> &x) override;

    void fini(adevs::Time clock) override;

    void setup_run(unsigned int run_number, unsigned int seed) noexcept;

    ScenarioType type() noexcept { return context_.scenario().type(); }

    std::string name() override { return context_.identifier(); }

  private:
    RuntimeContext context_;
    std::shared_ptr<UpdatableModule> ses_;
    std::shared_ptr<DemographicModule> demographic_;
    std::shared_ptr<RiskFactorHostModule> risk_factor_;
    std::shared_ptr<DiseaseModule> disease_;
    std::shared_ptr<UpdatableModule> analysis_;
    adevs::Time end_time_;

    void initialise_population();
    void update_population();
    void print_initial_population_statistics();

    void update_net_immigration();

    hgps::IntegerAgeGenderTable get_current_expected_population() const;
    hgps::IntegerAgeGenderTable get_current_simulated_population();
    void apply_net_migration(int net_value, unsigned int age, const core::Gender &gender);
    hgps::IntegerAgeGenderTable get_net_migration();
    hgps::IntegerAgeGenderTable create_net_migration();
    std::map<std::string, core::UnivariateSummary> create_input_data_summary() const;

    static Person partial_clone_entity(const Person &source) noexcept;
};

} // namespace hgps
