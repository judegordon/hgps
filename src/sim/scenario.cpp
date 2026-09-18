#include "scenario.h"

#include "diagnostics/internal_error.h"

#include <utility>

#include <fmt/format.h>

namespace hgps::sim {

void ScenarioJournal::record_migration(unsigned int run, int time, MigrationEntry entry) {
    const auto key = std::make_pair(run, time);
    if (migration_.contains(key)) {
        throw diag::InternalError(
            fmt::format("net migration for run {} year {} has already been recorded", run, time));
    }

    migration_.emplace(key, std::move(entry));
}

const MigrationEntry &ScenarioJournal::replay_migration(unsigned int run, int time) const {
    const auto found = migration_.find(std::make_pair(run, time));
    if (found == migration_.end()) {
        throw diag::InternalError(
            fmt::format("no net migration recorded for run {} year {}; the baseline and "
                        "intervention scenarios disagree about the horizon",
                        run, time));
    }

    return found->second;
}

void ScenarioJournal::record_residual_mortality(unsigned int run, int time,
                                                ResidualMortalityTable mortality) {
    const auto key = std::make_pair(run, time);
    if (residual_mortality_.contains(key)) {
        throw diag::InternalError(fmt::format(
            "residual mortality for run {} year {} has already been recorded", run, time));
    }

    residual_mortality_.emplace(key, std::move(mortality));
}

const ResidualMortalityTable &ScenarioJournal::replay_residual_mortality(unsigned int run,
                                                                         int time) const {
    const auto found = residual_mortality_.find(std::make_pair(run, time));
    if (found == residual_mortality_.end()) {
        throw diag::InternalError(
            fmt::format("no residual mortality recorded for run {} year {}; the baseline and "
                        "intervention scenarios disagree about the horizon",
                        run, time));
    }

    return found->second;
}

bool ScenarioJournal::contains_migration(unsigned int run, int time) const noexcept {
    return migration_.contains(std::make_pair(run, time));
}

bool ScenarioJournal::contains_residual_mortality(unsigned int run, int time) const noexcept {
    return residual_mortality_.contains(std::make_pair(run, time));
}

void ScenarioJournal::push_adjustment(unsigned int run, int time, AdjustmentTable adjustments) {
    adjustments_[std::make_pair(run, time)].push_back(std::move(adjustments));
}

const AdjustmentTable &ScenarioJournal::pop_adjustment(unsigned int run, int time) const {
    const auto key = std::make_pair(run, time);
    const auto found = adjustments_.find(key);
    if (found == adjustments_.end()) {
        throw diag::InternalError(fmt::format(
            "no risk-factor adjustments recorded for run {} year {}", run, time));
    }

    auto &cursor = adjustment_cursor_[key];
    if (cursor >= found->second.size()) {
        throw diag::InternalError(
            fmt::format("the intervention scenario asked for adjustment {} of run {} year {}, but "
                        "the baseline recorded only {}; the two scenarios' models disagree",
                        cursor + 1, run, time, found->second.size()));
    }

    return found->second[cursor++];
}

void ScenarioJournal::reset_adjustment_cursor(unsigned int run, int time) const {
    adjustment_cursor_[std::make_pair(run, time)] = 0;
}

void ScenarioJournal::clear() noexcept {
    migration_.clear();
    residual_mortality_.clear();
    adjustments_.clear();
    adjustment_cursor_.clear();
}

double BaselineScenario::apply(rng::RandomSource & /*random*/, model::Person & /*person*/,
                               int /*time*/, const core::Identifier & /*risk_factor_key*/,
                               double value) {
    return value;
}

SimplePolicyScenario::SimplePolicyScenario(config::InterventionSpec definition)
    : definition_{std::move(definition)}, name_{"Intervention"} {
    for (const auto &impact : definition_.impacts) {
        impacts_by_factor_[core::Identifier{impact.risk_factor}].push_back(impact);
    }
}

bool SimplePolicyScenario::is_active(int time) const noexcept {
    if (time < definition_.active_period.start_time) {
        return false;
    }
    if (definition_.active_period.finish_time.has_value() &&
        time > *definition_.active_period.finish_time) {
        return false;
    }
    return true;
}

double SimplePolicyScenario::apply(rng::RandomSource & /*random*/, model::Person &person, int time,
                                   const core::Identifier &risk_factor_key, double value) {
    if (!is_active(time)) {
        return value;
    }

    const auto found = impacts_by_factor_.find(risk_factor_key);
    if (found == impacts_by_factor_.end()) {
        return value;
    }

    // The impacts for a factor are applied in the order the config lists them, and the first age
    // band that covers this person wins — the bands in the shipped examples do not overlap, and
    // "first match" is what the baseline does.
    for (const auto &impact : found->second) {
        if (person.age < impact.from_age) {
            continue;
        }
        if (impact.to_age.has_value() && person.age > *impact.to_age) {
            continue;
        }

        // `absolute` shifts the value; anything else scales it, which is what the baseline's
        // simple policy does for a relative impact.
        if (definition_.impact_type.empty() || definition_.impact_type == "absolute") {
            return value + impact.impact_value;
        }
        return value * (1.0 + impact.impact_value);
    }

    return value;
}

std::unique_ptr<Scenario>
create_intervention_scenario(const config::InterventionSpec &definition,
                            api::BaselineCompat compat) {
    // The six upstream identifiers, spelled as upstream spells them.
    if (definition.identifier == "simple") {
        return std::make_unique<SimplePolicyScenario>(definition);
    }
    if (definition.identifier == "marketing") {
        return std::make_unique<MarketingScenario>(definition);
    }
    if (definition.identifier == "dynamic_marketing") {
        return std::make_unique<DynamicMarketingScenario>(definition);
    }
    if (definition.identifier == "fiscal") {
        return std::make_unique<FiscalScenario>(definition);
    }
    if (definition.identifier == "physical_activity") {
        return std::make_unique<PhysicalActivityScenario>(definition);
    }
    if (definition.identifier == "food_labelling") {
        return std::make_unique<FoodLabellingScenario>(definition, compat);
    }

    throw diag::InternalError(
        fmt::format("intervention '{}' is not implemented in this build; the config loader should "
                    "have rejected it",
                    definition.identifier));
}

} // namespace hgps::sim
