#include "engine.h"

#include "core/parallel.h"
#include "diagnostics/internal_error.h"
#include "random/seed.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace hgps::sim {
namespace {

using Clock = std::chrono::steady_clock;

void notify_scenario_completed(const RunHooks *hooks, const Engine &engine, unsigned int run,
                               Clock::time_point started, std::size_t years_completed) {
    if (hooks == nullptr || !hooks->scenario_completed) {
        return;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    hooks->scenario_completed(engine.type(), engine.name(), run, elapsed, years_completed);
}

} // namespace

Engine::Engine(std::shared_ptr<const model::ModelInput> inputs,
               std::unique_ptr<Scenario> scenario, Modules modules, std::uint32_t seed)
    : inputs_{std::move(inputs)}, modules_{std::move(modules)},
      context_{inputs_, std::move(scenario), seed} {
    if (!modules_.demographic || !modules_.ses || !modules_.risk_factor || !modules_.disease ||
        !modules_.analysis) {
        throw diag::InternalError("a simulation engine needs all five modules");
    }
}

ScenarioType Engine::type() const noexcept { return context_.scenario().type(); }

const std::string &Engine::name() const noexcept { return context_.scenario().name(); }

std::vector<ResultRow> Engine::run(unsigned int run, std::uint32_t run_seed,
                                   ScenarioJournal &journal, const RunHooks *hooks) {
    const auto start_year = static_cast<int>(inputs_->start_time());
    const auto stop_year = static_cast<int>(inputs_->stop_time());

    // The cohort size is a fraction of the real population in the start year.
    const auto real_population = modules_.demographic->get_total_population_size(start_year);
    const auto cohort_size =
        static_cast<std::size_t>(inputs_->settings().size_fraction *
                                 static_cast<double>(real_population));

    if (cohort_size == 0) {
        throw diag::InternalError(
            fmt::format("size_fraction {} of the {} population of {} rounds to no people at all",
                        inputs_->settings().size_fraction, start_year, real_population));
    }

    context_.start_run(run, run_seed, cohort_size);
    journal.reset_adjustment_cursor(run, start_year);

    std::vector<ResultRow> results;
    results.reserve(static_cast<std::size_t>(stop_year - start_year) + 1);

    const auto record = [&]() {
        results.push_back(ResultRow{.source = context_.scenario().type(),
                                    .source_name = context_.scenario().name(),
                                    .run = run,
                                    .time = context_.time_now(),
                                    .result = modules_.analysis->analyse(context_)});
    };

    if (hooks != nullptr && hooks->scenario_started) {
        hooks->scenario_started(context_.scenario().type(), context_.scenario().name(), run);
    }

    // Reading a clock and a population count changes nothing the simulation can see. The timing
    // is taken around each year rather than accumulated inside the modules so that a subscriber's
    // own cost is not charged to the year it is reporting on.
    const auto notify_year = [&](Clock::time_point year_start) {
        if (hooks == nullptr || !hooks->year_completed) {
            return;
        }
        const auto elapsed =
            std::chrono::duration<double, std::milli>(Clock::now() - year_start).count();
        hooks->year_completed(context_.scenario().type(), context_.scenario().name(), run,
                              context_.time_now(), elapsed,
                              context_.population().current_active_size());
    };

    auto year_start = Clock::now();
    initialise_population();
    record();
    notify_year(year_start);

    for (int year = start_year + 1; year <= stop_year; ++year) {
        // Between years, before any of the year's work: stopping here leaves a whole number of
        // simulated years, which is what makes a cancelled run a prefix of the full one.
        if (hooks != nullptr && hooks->is_cancelled()) {
            break;
        }

        year_start = Clock::now();
        context_.set_current_time(year);
        journal.reset_adjustment_cursor(run, year);

        update_population(journal);
        record();
        notify_year(year_start);
    }

    return results;
}

void Engine::initialise_population() {
    // Order is load-bearing. The single random stream is consumed in this order, so changing it
    // changes every result: demographics decide who exists, SES is a predictor for the risk
    // factors, the risk factors drive disease incidence, and the analysis reads the outcome.
    modules_.demographic->initialise_population(context_);
    modules_.ses->initialise_population(context_);
    modules_.risk_factor->initialise_population(context_);
    modules_.disease->initialise_population(context_);
    modules_.analysis->initialise_population(context_);
}

void Engine::update_population(ScenarioJournal &journal) {
    // The same order, for the same reason.
    modules_.demographic->update_population(context_, *modules_.disease, journal);

    apply_net_migration(net_migration(journal));

    modules_.ses->update_population(context_);
    modules_.risk_factor->update_population(context_);
    modules_.disease->update_population(context_);
    modules_.analysis->update_population(context_);
}

model::IntegerAgeGenderTable Engine::expected_population() const {
    const auto start_year = context_.start_time();
    const auto real_start_population =
        modules_.demographic->get_total_population_size(start_year);
    // The cohort size is the *whole number* of people the run actually started with, not the
    // unrounded fraction. That matters: the scale below is applied to a projected age-sex count
    // and rounded to a person, so scaling by `cohort_size / real_population` rather than by
    // `size_fraction` moves some bands across the rounding boundary. Using the fraction directly
    // left the reference example's population differing from the baseline's by a person or two
    // per year, which then showed up in every count-weighted aggregate.
    const auto cohort_size = static_cast<double>(static_cast<std::size_t>(
        inputs_->settings().size_fraction * static_cast<double>(real_start_population)));

    const auto &distribution =
        modules_.demographic->get_population_distribution(context_.time_now());

    auto expected = model::create_age_gender_table<int>(context_.age_range());
    for (int age = context_.age_range().lower(); age <= context_.age_range().upper(); ++age) {
        const auto found = distribution.find(age);
        if (found == distribution.end()) {
            continue;
        }

        const double scale = cohort_size / static_cast<double>(real_start_population);
        expected.at(age, core::Gender::male) =
            static_cast<int>(std::round(static_cast<double>(found->second.males) * scale));
        expected.at(age, core::Gender::female) =
            static_cast<int>(std::round(static_cast<double>(found->second.females) * scale));
    }

    return expected;
}

model::IntegerAgeGenderTable Engine::simulated_population() const {
    auto counts = model::create_age_gender_table<int>(context_.age_range());

    // Serial, in slot order. The baseline counts this with a mutex-guarded increment inside a
    // parallel loop; a count of a few thousand people does not need a thread, and the mutex made
    // it slower than the loop it replaced.
    for (const auto &person : context_.population()) {
        if (!person.is_active()) {
            continue;
        }
        const auto age = static_cast<int>(person.age);
        if (counts.contains(age, person.gender)) {
            counts.at(age, person.gender)++;
        }
    }

    return counts;
}

MigrationEntry Engine::net_migration(ScenarioJournal &journal) {
    if (context_.scenario().type() != ScenarioType::baseline) {
        // The intervention must see the baseline's migration, or the two futures would differ by
        // sampling noise as well as by the policy (ADR 0009).
        return journal.replay_migration(context_.current_run(), context_.time_now());
    }

    const auto expected = expected_population();
    const auto simulated = simulated_population();

    MigrationEntry entry;
    for (int age = context_.age_range().lower(); age <= context_.age_range().upper(); ++age) {
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            entry.net_by_age_gender[age][gender] =
                expected.at(age, gender) - simulated.at(age, gender);
        }
    }

    journal.record_migration(context_.current_run(), context_.time_now(), entry);
    return entry;
}

model::Person Engine::clone_for_immigration(const model::Person &source) {
    model::Person clone;
    clone.age = source.age;
    clone.gender = source.gender;
    clone.region = source.region;
    clone.ethnicity = source.ethnicity;
    clone.ses = source.ses;
    clone.sector = source.sector;
    clone.income = source.income;
    clone.income_continuous = source.income_continuous;
    clone.physical_activity = source.physical_activity;
    clone.risk_factors = source.risk_factors;

    for (const auto &[disease, state] : source.diseases) {
        clone.diseases.emplace(disease, state.clone());
    }

    return clone;
}

void Engine::apply_net_migration(const MigrationEntry &migration) {
    auto &population = context_.population();
    const auto now = static_cast<unsigned int>(context_.time_now());

    // This year's figures, set unconditionally so the series has a value every year rather than
    // only in the years something went wrong. See the empty-band branch below.
    context_.metrics()["ImmigrationShortfallPeople"] = 0.0;
    context_.metrics()["ImmigrationShortfallBands"] = 0.0;

    // Ascending age, then male before female: the migration map is ordered, so the draws below
    // happen in a stated sequence.
    for (const auto &[age, by_gender] : migration.net_by_age_gender) {
        for (const auto &[gender, net] : by_gender) {
            if (net == 0) {
                continue;
            }

            if (net > 0) {
                // Immigrants are modelled on people already here of the same age and sex. The
                // candidate list is built by walking the population in slot order — the baseline
                // gathers it from parallel tasks under a mutex and then sorts it, which is the
                // same list by a longer route (audit N-8).
                std::vector<std::size_t> candidates;
                for (std::size_t index = 0; index < population.size(); ++index) {
                    const auto &person = population[index];
                    if (person.is_active() && static_cast<int>(person.age) == age &&
                        person.gender == gender) {
                        candidates.push_back(index);
                    }
                }

                if (candidates.empty()) {
                    // Nobody of this age and sex to model an immigrant on, so the cohort falls
                    // short of the demographic projection it is otherwise pinned to. The baseline
                    // does the same and says nothing (deviation B-21); this at least reports it,
                    // so a run's own metrics say how many people it is short and in how many
                    // bands rather than leaving it to be discovered by comparison.
                    context_.metrics()["ImmigrationShortfallPeople"] += static_cast<double>(net);
                    context_.metrics()["ImmigrationShortfallBands"] += 1.0;
                    continue;
                }

                for (int added = 0; added < net; ++added) {
                    // Half-open, so every candidate is reachable. The baseline passes
                    // size() - 1 to an inclusive next_int, which reaches the same set and would
                    // silently drop the last candidate if that contract were corrected (B-07).
                    const auto chosen = context_.random().next_int(candidates.size());
                    population.add(clone_for_immigration(population[candidates.at(chosen)]), now);
                }

                continue;
            }

            // Emigration: the first `-net` active people of this age and sex, in slot order.
            int remaining = -net;
            for (std::size_t index = 0; index < population.size() && remaining > 0; ++index) {
                const auto &person = population[index];
                if (person.is_active() && static_cast<int>(person.age) == age &&
                    person.gender == gender) {
                    population.mark_emigrated(index, now);
                    --remaining;
                }
            }
        }
    }
}

Runner::Outcome Runner::run(Engine &baseline, unsigned int trial_runs,
                            std::uint32_t master_seed, const ResultSink &sink,
                            const RunHooks *hooks) {
    if (trial_runs < 1) {
        throw diag::InternalError("The number of trial runs must not be less than one.");
    }
    if (baseline.type() != ScenarioType::baseline) {
        throw diag::InternalError(
            fmt::format("Simulation '{}' cannot be evaluated alone", baseline.name()));
    }

    const auto start = Clock::now();
    Outcome outcome;

    for (unsigned int run = 1; run <= trial_runs; ++run) {
        if (hooks != nullptr && hooks->is_cancelled()) {
            outcome.cancelled = true;
            break;
        }

        // Derived, not drawn: adding a trial run does not change the earlier runs' seeds
        // (ADR 0015).
        const auto run_seed = rng::derive_run_seed(master_seed, run - 1);

        journal_.clear();
        const auto scenario_start = Clock::now();
        const auto results = baseline.run(run, run_seed, journal_, hooks);
        notify_scenario_completed(hooks, baseline, run, scenario_start, results.size());

        for (const auto &row : results) {
            sink(row);
        }
        outcome.years_completed += results.size();
    }

    if (hooks != nullptr && hooks->is_cancelled()) {
        outcome.cancelled = true;
    }
    outcome.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return outcome;
}

Runner::Outcome Runner::run(Engine &baseline, Engine &intervention, unsigned int trial_runs,
                            std::uint32_t master_seed, const ResultSink &sink,
                            const RunHooks *hooks) {
    if (trial_runs < 1) {
        throw diag::InternalError("The number of trial runs must not be less than one.");
    }
    if (baseline.type() != ScenarioType::baseline) {
        throw diag::InternalError(
            fmt::format("Baseline simulation '{}' type mismatch.", baseline.name()));
    }
    if (intervention.type() != ScenarioType::intervention) {
        throw diag::InternalError(
            fmt::format("Intervention simulation '{}' type mismatch.", intervention.name()));
    }

    const auto start = Clock::now();
    Outcome outcome;

    for (unsigned int run = 1; run <= trial_runs; ++run) {
        if (hooks != nullptr && hooks->is_cancelled()) {
            outcome.cancelled = true;
            break;
        }

        const auto run_seed = rng::derive_run_seed(master_seed, run - 1);

        // The baseline runs first and fills the journal; the intervention then replays it. Both
        // get the same run seed, which is the common-random-numbers method: the difference
        // between the two futures is the policy, not sampling noise.
        journal_.clear();

        auto scenario_start = Clock::now();
        const auto baseline_results = baseline.run(run, run_seed, journal_, hooks);
        notify_scenario_completed(hooks, baseline, run, scenario_start, baseline_results.size());

        // The intervention replays the baseline's journal year by year, so it cannot outrun it: a
        // cancellation that stopped the baseline short stops the intervention at the same year,
        // and the pair stays comparable.
        scenario_start = Clock::now();
        const auto intervention_results = intervention.run(run, run_seed, journal_, hooks);
        notify_scenario_completed(hooks, intervention, run, scenario_start,
                                  intervention_results.size());

        // Scenario order, then year order — the output's row order (ADR 0020).
        for (const auto &row : baseline_results) {
            sink(row);
        }
        for (const auto &row : intervention_results) {
            sink(row);
        }
        outcome.years_completed += baseline_results.size() + intervention_results.size();
    }

    if (hooks != nullptr && hooks->is_cancelled()) {
        outcome.cancelled = true;
    }
    outcome.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return outcome;
}

} // namespace hgps::sim
