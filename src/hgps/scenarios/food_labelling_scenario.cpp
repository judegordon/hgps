#include "food_labelling_scenario.h"

#include <functional>
#include <stdexcept>

namespace hgps {

inline constexpr int FOP_NO_EFFECT = -2;

FoodLabellingScenario::FoodLabellingScenario(FoodLabellingDefinition &&definition)
    : definition_{std::move(definition)} {
    if (definition_.impacts.empty()) {
        throw std::invalid_argument("Number of impact levels mismatch, must be greater than 1.");
    }

    auto age = 0u;
    for (const auto &level : definition_.impacts) {
        if (level.from_age < age) {
            throw std::out_of_range("Impact levels must be non-overlapping and ordered.");
        }
        age = level.from_age + 1u;
    }
}

std::size_t FoodLabellingScenario::make_book_key(
    const std::size_t entity_id, const core::Identifier &risk_factor_key) noexcept {
    return entity_id ^
           (std::hash<core::Identifier>{}(risk_factor_key) + 0x9e3779b9 + (entity_id << 6) +
            (entity_id >> 2));
}

const PolicyImpact *FoodLabellingScenario::find_active_impact(
    const core::Identifier &risk_factor_key, const unsigned int age) const noexcept {
    const PolicyImpact *active = nullptr;
    for (const auto &impact : definition_.impacts) {
        if (impact.risk_factor != risk_factor_key) {
            continue;
        }
        if (age >= impact.from_age) {
            active = &impact;
        } else {
            break;
        }
    }
    return active;
}

void FoodLabellingScenario::clear() noexcept { interventions_book_.clear(); }

double FoodLabellingScenario::apply(Random &generator, Person &entity, const int time,
                                    const core::Identifier &risk_factor_key, const double value) {
    if (!definition_.active_period.contains(time)) {
        return value;
    }

    const auto *impact_def = find_active_impact(risk_factor_key, entity.age);
    if (impact_def == nullptr) {
        return value;
    }

    const auto book_key = make_book_key(entity.id(), risk_factor_key);
    auto impact = value;
    const auto probability = generator.next_double();
    const auto time_since_start =
        static_cast<unsigned int>(time - definition_.active_period.start_time);

    if (time_since_start < definition_.coverage.cutoff_time) {
        if (!interventions_book_.contains(book_key) ||
            interventions_book_.at(book_key) == FOP_NO_EFFECT) {
            if (probability < definition_.coverage.short_term_rate) {
                impact += calculate_policy_impact(entity, *impact_def);
                interventions_book_.insert_or_assign(book_key, time);
            } else {
                interventions_book_.insert_or_assign(book_key, FOP_NO_EFFECT);
            }
        }
    } else if (!interventions_book_.contains(book_key)) {
        if (probability < definition_.coverage.long_term_rate) {
            impact += calculate_policy_impact(entity, *impact_def);
            interventions_book_.emplace(book_key, time);
        } else {
            interventions_book_.emplace(book_key, FOP_NO_EFFECT);
        }
    }

    return impact;
}

const PolicyInterval &FoodLabellingScenario::active_period() const noexcept {
    return definition_.active_period;
}

const std::vector<PolicyImpact> &FoodLabellingScenario::impacts() const noexcept {
    return definition_.impacts;
}

double FoodLabellingScenario::calculate_policy_impact(const Person &entity,
                                                      const PolicyImpact &impact) const noexcept {
    const auto current_risk_factor_value =
        entity.get_risk_factor_value(definition_.adjustment_risk_factor.identifier);
    const auto transfer_coefficient =
        definition_.transfer_coefficient.get_value(entity.gender, entity.age);
    const double adjustment_factor = definition_.adjustment_risk_factor.adjustment;

    return transfer_coefficient * impact.value * current_risk_factor_value * adjustment_factor;
}

} // namespace hgps