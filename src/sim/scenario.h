// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{scenario,intervention_scenario,simple_policy_scenario}.h.
#pragma once

#include "config/types.h"
#include "core/identifier.h"
#include "model/containers.h"
#include "model/person.h"
#include "random/source.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
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

/// @brief The shared machinery of the age-banded interventions.
///
/// `marketing`, `dynamic_marketing`, `food_labelling`, `physical_activity` and `fiscal` all work
/// the same way: they carry ordered, non-overlapping age bands of impacts, they remember what
/// they have already done to each person, and they apply the *difference* when a person moves
/// from one band to the next rather than the whole new impact. What differs between them is the
/// rule for deciding whether a person is exposed at all, which is the one virtual function below.
class BandedInterventionScenario : public Scenario {
  public:
    explicit BandedInterventionScenario(config::InterventionSpec definition,
                                        std::size_t required_bands);

    ScenarioType type() const noexcept override { return ScenarioType::intervention; }
    const std::string &name() const noexcept override { return name_; }

    double apply(rng::RandomSource &random, model::Person &person, int time,
                 const core::Identifier &risk_factor_key, double value) override;

    /// @brief Forgets who has been exposed. Called at the start of each run.
    void clear() noexcept override { book_.clear(); }

    const config::InterventionSpec &definition() const noexcept { return definition_; }

  protected:
    /// @brief What the policy does to `value` for this person this year.
    virtual double impact_for(rng::RandomSource &random, model::Person &person, int time,
                              const core::Identifier &risk_factor_key, double value) = 0;

    bool is_active(int time) const noexcept;
    bool affects(const core::Identifier &risk_factor_key) const noexcept;

    /// @brief The index of the impact band containing an age, or -1.
    int band_of(unsigned int age) const noexcept;

    const std::vector<config::PolicyImpact> &impacts() const noexcept {
        return definition_.impacts;
    }

    /// @brief Sentinels for the exposure book, chosen so they can never be a band index.
    static constexpr int kNeverExposed = -1;
    static constexpr int kNoEffect = -2;
    static constexpr int kFormerlyExposed = -3;

    /// @brief What has already been done to a person: a band index, or one of the sentinels.
    std::map<std::size_t, int> book_;

    /// @brief The impact of band `index`, or 0 for a sentinel.
    double impact_value(int index) const noexcept;

  private:
    config::InterventionSpec definition_;
    std::set<core::Identifier> factors_;
    std::string name_{"Intervention"};
};

/// @brief `marketing`: a one-off shift when a person first enters an age band, and the difference
///        when they move up to the next one. Everybody is exposed.
class MarketingScenario final : public BandedInterventionScenario {
  public:
    explicit MarketingScenario(config::InterventionSpec definition);

  protected:
    double impact_for(rng::RandomSource &random, model::Person &person, int time,
                      const core::Identifier &risk_factor_key, double value) override;
};

/// @brief `dynamic_marketing`: `marketing` with three transition probabilities — alpha to become
///        exposed, beta to lapse, gamma to take it up again after lapsing.
class DynamicMarketingScenario final : public BandedInterventionScenario {
  public:
    explicit DynamicMarketingScenario(config::InterventionSpec definition);

  protected:
    double impact_for(rng::RandomSource &random, model::Person &person, int time,
                      const core::Identifier &risk_factor_key, double value) override;

  private:
    double alpha_{1.0};
    double beta_{0.0};
    double gamma_{0.0};
};

/// @brief `fiscal`: `marketing`, but the impact is a *proportion* of the person's own current
///        value rather than a fixed amount — a tax changes what you buy in proportion to what you
///        were buying.
class FiscalScenario final : public BandedInterventionScenario {
  public:
    /// @brief What happens when somebody who was exposed as an adolescent becomes an adult.
    ///        `pessimist` applies the difference; `optimist` keeps the adolescent effect.
    enum class ImpactType : std::uint8_t { pessimist, optimist };

    explicit FiscalScenario(config::InterventionSpec definition);

  protected:
    double impact_for(rng::RandomSource &random, model::Person &person, int time,
                      const core::Identifier &risk_factor_key, double value) override;

  private:
    ImpactType impact_type_{ImpactType::pessimist};
};

/// @brief `physical_activity`: a coverage-rate draw decides once, in childhood, whether a person
///        is ever affected; those who are get the child effect and later the adult one.
class PhysicalActivityScenario final : public BandedInterventionScenario {
  public:
    explicit PhysicalActivityScenario(config::InterventionSpec definition);

  protected:
    double impact_for(rng::RandomSource &random, model::Person &person, int time,
                      const core::Identifier &risk_factor_key, double value) override;

  private:
    double coverage_rate_{0.0};
};

/// @brief `food_labelling`: a coverage-rate draw decides whether a person notices the label, with
///        a higher rate in the policy's first years than afterwards; the impact is a product of
///        the effect, an adjustment factor, the person's value of an adjusted risk factor, and a
///        transfer coefficient that depends on their sex and whether they are a child.
class FoodLabellingScenario final : public BandedInterventionScenario {
  public:
    explicit FoodLabellingScenario(config::InterventionSpec definition);

  protected:
    double impact_for(rng::RandomSource &random, model::Person &person, int time,
                      const core::Identifier &risk_factor_key, double value) override;

  private:
    double short_term_rate_{0.0};
    double long_term_rate_{0.0};
    unsigned int cutoff_time_{0};
    unsigned int child_cutoff_age_{0};

    core::Identifier adjustment_factor_;
    double adjustment_value_{0.0};

    /// @brief Child male, child female, adult male, adult female.
    std::array<double, 4> transfer_{};

    double transfer_for(const model::Person &person) const noexcept;
};

/// @brief Builds the scenario an intervention specification names.
/// @throws diag::InternalError for an identifier this build does not implement — the config
///         loader has already rejected those, so reaching here is a bug (ADR 0021).
std::unique_ptr<Scenario> create_intervention_scenario(const config::InterventionSpec &definition);

} // namespace hgps::sim
