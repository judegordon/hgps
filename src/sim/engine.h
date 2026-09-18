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

#include <cstddef>
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

/// @brief Watches a run go by, and can ask it to stop.
///
/// The one channel through which anything outside the simulation learns that a year has finished.
/// It is deliberately narrow: an observer receives copies of numbers the run has already computed
/// and returns nothing the run reads, so there is no way for one to consume a random draw, reorder
/// work, or change a result. `hgps::api::EventSubscriber` is the public face of this
/// (docs/decisions/0033-an-event-stream-the-simulation-cannot-see.md).
///
/// Cancellation lives here too, as a predicate the runner consults **between** years and between
/// scenarios rather than inside a population sweep — so a cancelled run is a prefix of the run that
/// would have happened, never a half-updated cohort.
struct RunHooks {
    /// @brief Called as each scenario starts. `run` is the 1-based trial run number.
    std::function<void(ScenarioType type, const std::string &name, unsigned int run)>
        scenario_started{};

    /// @brief Called as each simulated year finishes, with the wall-clock time it took and the
    ///        number of people alive and present at the end of it.
    std::function<void(ScenarioType type, const std::string &name, unsigned int run, int year,
                       double elapsed_ms, std::size_t population_size)>
        year_completed{};

    /// @brief Called as each scenario finishes, with how many years it actually simulated.
    std::function<void(ScenarioType type, const std::string &name, unsigned int run,
                       double elapsed_ms, std::size_t years_completed)>
        scenario_completed{};

    /// @brief True when the run should stop at its next safe point. Never called from inside a
    ///        population sweep.
    std::function<bool()> cancelled{};

    bool is_cancelled() const { return cancelled && cancelled(); }
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
    /// @param hooks Progress and cancellation, or null for neither.
    std::vector<ResultRow> run(unsigned int run, std::uint32_t run_seed, ScenarioJournal &journal,
                               const RunHooks *hooks = nullptr);

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

    /// @brief What one call to `run` did.
    struct Outcome {
        /// @brief Wall-clock milliseconds taken.
        double elapsed_ms{};

        /// @brief True if the hooks' cancellation predicate stopped it short of the horizon.
        bool cancelled{};

        /// @brief How many scenario-years were simulated and handed to the sink.
        std::size_t years_completed{};
    };

    /// @brief Runs `trial_runs` runs of the baseline alone.
    Outcome run(Engine &baseline, unsigned int trial_runs, std::uint32_t master_seed,
                const ResultSink &sink, const RunHooks *hooks = nullptr);

    /// @brief Runs `trial_runs` runs of the baseline then the intervention, sharing each run's
    ///        seed — the common-random-numbers method the model's whole purpose rests on.
    Outcome run(Engine &baseline, Engine &intervention, unsigned int trial_runs,
                std::uint32_t master_seed, const ResultSink &sink,
                const RunHooks *hooks = nullptr);

  private:
    ScenarioJournal journal_;
};

} // namespace hgps::sim
