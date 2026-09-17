// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{scenario,intervention_scenario,simple_policy_scenario}.h.
#pragma once

#include "config/types.h"
#include "core/identifier.h"
#include "model/containers.h"
#include "model/person.h"
#include "random/source.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hgps::sim {

enum class ScenarioType : std::uint8_t { baseline, intervention };

/// @brief The net migration the baseline scenario computed for one year.
///
/// The intervention scenario must see the same migration as the baseline, or the two futures
/// would differ by sampling noise as well as by the policy. The baseline passes these numbers
/// between two concurrently running scenarios over a channel with a timeout; here the baseline
/// scenario records them and the intervention replays them
/// (docs/decisions/0009-sequential-scenarios-and-the-migration-journal.md).
/// @brief Residual mortality by age and sex.
using ResidualMortalityTable = model::GenderTable<int, double>;

/// @brief Risk-factor adjustment deltas by sex and factor, indexed by age.
using AdjustmentTable = model::Map2d<core::Gender, core::Identifier, std::vector<double>>;

struct MigrationEntry {
    /// @brief Net migrants by age and sex. Negative means emigration.
    std::map<int, std::map<core::Gender, int>> net_by_age_gender;
};

/// @brief Everything the baseline scenario computes that the intervention must reuse.
///
/// Two things travel this way, and the baseline passes both between its concurrently running
/// scenarios over a channel with a timeout: the year's net migration, and the residual-mortality
/// table (the mortality left once modelled disease mortality is accounted for). Both must be
/// identical in the two futures, or they would differ by sampling noise as well as by the policy.
class ScenarioJournal {
  public:
    /// @throws diag::InternalError if the year is already recorded — the baseline scenario runs
    ///         each year exactly once, so a second write means the engine has lost track.
    void record_migration(unsigned int run, int time, MigrationEntry entry);

    /// @throws diag::InternalError if the year was never recorded, which means the two scenarios
    ///         disagree about the horizon.
    const MigrationEntry &replay_migration(unsigned int run, int time) const;

    void record_residual_mortality(unsigned int run, int time,
                                   ResidualMortalityTable mortality);

    /// @brief Appends a risk-factor adjustment table for a year.
    ///
    /// A year may have several: one per adjusting model, and one per income stratum where that is
    /// enabled. They are replayed in the order they were recorded, which is the same order the
    /// intervention's models ask for them — the baseline relies on exactly that property, with a
    /// channel instead of a journal. A mismatch exhausts the queue and throws, rather than
    /// silently applying the wrong table.
    void push_adjustment(unsigned int run, int time, AdjustmentTable adjustments);

    /// @brief The next adjustment table for a year.
    /// @throws diag::InternalError if the baseline recorded fewer than the intervention asks for.
    const AdjustmentTable &pop_adjustment(unsigned int run, int time) const;

    /// @brief Forgets how far through a year's adjustments the replay has got, so a rerun of the
    ///        same year starts again.
    void reset_adjustment_cursor(unsigned int run, int time) const;

    /// @throws diag::InternalError if the year was never recorded.
    const ResidualMortalityTable &replay_residual_mortality(unsigned int run, int time) const;

    bool contains_migration(unsigned int run, int time) const noexcept;
    bool contains_residual_mortality(unsigned int run, int time) const noexcept;

    std::size_t size() const noexcept { return migration_.size(); }
    void clear() noexcept;

  private:
    std::map<std::pair<unsigned int, int>, MigrationEntry> migration_;
    std::map<std::pair<unsigned int, int>, ResidualMortalityTable> residual_mortality_;
    std::map<std::pair<unsigned int, int>, std::vector<AdjustmentTable>> adjustments_;

    // Mutable: replaying is logically a read of the journal, and the cursor is bookkeeping for
    // that read rather than part of the journal's contents.
    mutable std::map<std::pair<unsigned int, int>, std::size_t> adjustment_cursor_;
};

/// @brief A future to simulate: the baseline, or the baseline plus a policy.
class Scenario {
  public:
    virtual ~Scenario() = default;

    virtual ScenarioType type() const noexcept = 0;
    virtual const std::string &name() const noexcept = 0;

    /// @brief Transforms a risk factor value for a person at a time.
    ///
    /// The baseline scenario returns the value unchanged. An intervention applies its policy.
    virtual double apply(rng::RandomSource &random, model::Person &person, int time,
                         const core::Identifier &risk_factor_key, double value) = 0;

    /// @brief Resets any per-run state.
    virtual void clear() noexcept = 0;
};

/// @brief The future with no policy in it.
class BaselineScenario final : public Scenario {
  public:
    ScenarioType type() const noexcept override { return ScenarioType::baseline; }
    const std::string &name() const noexcept override { return name_; }

    double apply(rng::RandomSource &random, model::Person &person, int time,
                 const core::Identifier &risk_factor_key, double value) override;

    void clear() noexcept override {}

  private:
    std::string name_{"Baseline"};
};

/// @brief The `simple` intervention: a flat shift to named risk factors over an age band, for the
///        years the policy is active.
class SimplePolicyScenario final : public Scenario {
  public:
    explicit SimplePolicyScenario(config::InterventionSpec definition);

    ScenarioType type() const noexcept override { return ScenarioType::intervention; }
    const std::string &name() const noexcept override { return name_; }

    double apply(rng::RandomSource &random, model::Person &person, int time,
                 const core::Identifier &risk_factor_key, double value) override;

    void clear() noexcept override {}

    const config::InterventionSpec &definition() const noexcept { return definition_; }

  private:
    config::InterventionSpec definition_;
    std::string name_;

    /// Impacts indexed by risk factor, so applying one is a map lookup rather than a scan of
    /// every impact for every factor of every person every year.
    std::map<core::Identifier, std::vector<config::PolicyImpact>> impacts_by_factor_;

    bool is_active(int time) const noexcept;
};

/// @brief Builds the scenario an intervention specification names.
/// @throws diag::InternalError for an identifier this build does not implement — the config
///         loader has already rejected those, so reaching here is a bug (ADR 0021).
std::unique_ptr<Scenario> create_intervention_scenario(const config::InterventionSpec &definition);

} // namespace hgps::sim
