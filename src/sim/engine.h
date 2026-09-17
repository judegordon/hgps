// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{simulation,runner}.{h,cpp}, less the adevs DEVS scaffolding.
#pragma once

#include "model/analysis/analysis_module.h"
#include "model/containers.h"
#include "model/demographic.h"
#include "model/disease/disease_model.h"
#include "model/results.h"
#include "model/riskfactor/risk_factor_model.h"
#include "model/runtime_context.h"
#include "model/ses_module.h"
#include "scenario.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hgps::sim {

/// @brief One scenario's five modules.
struct Modules {
    std::unique_ptr<model::DemographicModule> demographic;
    std::unique_ptr<model::SesNoiseModule> ses;
    std::unique_ptr<model::RiskFactorHostModule> risk_factor;
    std::unique_ptr<model::DiseaseModule> disease;
    std::unique_ptr<model::AnalysisModule> analysis;
};

/// @brief One year of one scenario's results, tagged with where it came from.
struct ResultRow {
    ScenarioType source{};
    std::string source_name;
    unsigned int run{};
    int time{};
    model::ModelResult result;
};

/// @brief Runs one scenario over the horizon.
///
/// The baseline wraps this in the vendored adevs discrete-event simulator, which buys a clock and
/// a message queue that a `for` loop over the years provides directly. Dropping it removes a
/// vendored dependency and makes the year loop readable.
class Engine {
  public:
    Engine() = delete;

    Engine(std::shared_ptr<const model::ModelInput> inputs, std::unique_ptr<Scenario> scenario,
           Modules modules, std::uint32_t seed);

    ScenarioType type() const noexcept;
    const std::string &name() const noexcept;

    /// @brief Runs one trial run and returns one result per simulated year, in year order.
    ///
    /// @param run The 1-based trial run number.
    /// @param run_seed The seed for this run, the same for both scenarios.
    /// @param journal Written by the baseline scenario, read by the intervention.
    std::vector<ResultRow> run(unsigned int run, std::uint32_t run_seed,
                               ScenarioJournal &journal);

  private:
    std::shared_ptr<const model::ModelInput> inputs_;
    Modules modules_;
    model::RuntimeContext context_;

    void initialise_population();
    void update_population(ScenarioJournal &journal);

    /// @brief Net migration for the year: the baseline computes it, the intervention replays it.
    MigrationEntry net_migration(ScenarioJournal &journal);

    /// @brief The population the data says there should be this year, scaled to the cohort.
    model::IntegerAgeGenderTable expected_population() const;

    /// @brief The population there actually is, by age and sex.
    model::IntegerAgeGenderTable simulated_population() const;

    void apply_net_migration(const MigrationEntry &migration);

    /// @brief A new arrival modelled on an existing person of the same age and sex.
    ///
    /// Immigrants arrive with the characteristics of someone already here, because nothing else
    /// in the model describes them. The clone gets a fresh identifier from the population.
    static model::Person clone_for_immigration(const model::Person &source);
};

/// @brief Runs the scenarios of each trial run, one after the other.
///
/// Sequential, always: this is where the baseline starts a jthread per scenario, and where its
/// output row order stops being reproducible (audit B-01, ADR 0009).
class Runner {
  public:
    /// @brief Called with each year's results as they are produced, in scenario then year order.
    using ResultSink = std::function<void(const ResultRow &)>;

    Runner() = default;

    /// @brief Runs `trial_runs` runs of the baseline alone.
    /// @return The wall-clock milliseconds taken.
    double run(Engine &baseline, unsigned int trial_runs, std::uint32_t master_seed,
               const ResultSink &sink);

    /// @brief Runs `trial_runs` runs of the baseline then the intervention, sharing each run's
    ///        seed — the common-random-numbers method the model's whole purpose rests on.
    double run(Engine &baseline, Engine &intervention, unsigned int trial_runs,
               std::uint32_t master_seed, const ResultSink &sink);

  private:
    ScenarioJournal journal_;
};

} // namespace hgps::sim
