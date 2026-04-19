#include "fiscal_scenario.h"

#include "hgps_core/utils/string_util.h"

#include <fmt/core.h>

#include <functional>
#include <stdexcept>

namespace hgps {

FiscalPolicyScenario::FiscalPolicyScenario(FiscalPolicyDefinition &&definition)
    : definition_{std::move(definition)} {
    if (definition_.impacts.size() != 3) {
        throw std::invalid_argument("Number of impact levels mismatch, must be 3.");
    }

    auto age = 0u;
    for (const auto &level : definition_.impacts) {
        if (level.from_age < age) {
            throw std::out_of_range("Impact levels must be non-overlapping and ordered.");
        }
        age = level.from_age + 1u;
    }
}

std::size_t FiscalPolicyScenario::make_book_key(const std::size_t entity_id,
                                                const core::Identifier &risk_factor_key) noexcept {
    return entity_id ^
           (std::hash<core::Identifier>{}(risk_factor_key) + 0x9e3779b9 + (entity_id << 6) +
            (entity_id >> 2));
}

void FiscalPolicyScenario::clear() noexcept { interventions_book_.clear(); }

double FiscalPolicyScenario::apply([[maybe_unused]] Random &generator, Person &entity, const int time,
                                   const core::Identifier &risk_factor_key, const double value) {
    if (!definition_.active_period.contains(time)) {
        return value;
    }

    const auto book_key = make_book_key(entity.id(), risk_factor_key);
    const auto age = entity.age;
    auto &child_effect = definition_.impacts.at(0);

    if (age < child_effect.from_age) {
        return value;
    }

    const auto factor_value = entity.get_risk_factor_value(risk_factor_key);

    if (child_effect.risk_factor != risk_factor_key &&
        definition_.impacts.at(1).risk_factor != risk_factor_key &&
        definition_.impacts.at(2).risk_factor != risk_factor_key) {
        return value;
    }

    if (child_effect.contains(age)) {
        if (!interventions_book_.contains(book_key)) {
            interventions_book_.emplace(book_key, age);
            return value + (factor_value * child_effect.value);
        }
        return value;
    }

    auto &teen_effect = definition_.impacts.at(1);
    if (teen_effect.contains(age)) {
        if (!interventions_book_.contains(book_key)) {
            interventions_book_.emplace(book_key, age);
            return value + (factor_value * teen_effect.value);
        }

        if (child_effect.contains(interventions_book_.at(book_key))) {
            interventions_book_.at(book_key) = age;
            const auto effect = teen_effect.value - child_effect.value;
            return value + (factor_value * effect);
        }

        return value;
    }

    auto &adult_effect = definition_.impacts.at(2);
    if (age >= adult_effect.from_age) {
        if (!interventions_book_.contains(book_key)) {
            interventions_book_.emplace(book_key, age);
            return value + (factor_value * adult_effect.value);
        }

        if (teen_effect.contains(interventions_book_.at(book_key))) {
            if (definition_.impact_type == FiscalImpactType::pessimist) {
                interventions_book_.at(book_key) = age;
                const auto effect = adult_effect.value - teen_effect.value;
                return value + (factor_value * effect);
            }
            if (definition_.impact_type == FiscalImpactType::optimist) {
                return value;
            }

            throw std::logic_error("Fiscal intervention impact type not implemented.");
        }

        return value;
    }

    throw std::logic_error(
        fmt::format("Fiscal intervention error, age {} outside the valid range.", age));
}

const PolicyInterval &FiscalPolicyScenario::active_period() const noexcept {
    return definition_.active_period;
}

const std::vector<PolicyImpact> &FiscalPolicyScenario::impacts() const noexcept {
    return definition_.impacts;
}

FiscalImpactType parse_fiscal_impact_type(const std::string &impact_type) {
    if (core::case_insensitive::equals(impact_type, "pessimist")) {
        return FiscalImpactType::pessimist;
    }
    if (core::case_insensitive::equals(impact_type, "optimist")) {
        return FiscalImpactType::optimist;
    }

    throw std::logic_error("Unknown fiscal policy impact type: " + impact_type);
}

} // namespace hgps