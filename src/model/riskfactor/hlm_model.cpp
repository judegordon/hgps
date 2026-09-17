#include "hlm_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {

StaticHierarchicalLinearModel::StaticHierarchicalLinearModel(
    std::shared_ptr<const std::map<core::Identifier, LinearEquation>> models,
    std::shared_ptr<const std::map<int, HierarchicalLevel>> levels)
    : models_{std::move(models)}, levels_{std::move(levels)} {
    if (!models_ || models_->empty()) {
        throw diag::InternalError("The hierarchical model equations definition must not be empty");
    }
    if (!levels_ || levels_->empty()) {
        throw diag::InternalError("The hierarchical model levels definition must not be empty");
    }
}

void StaticHierarchicalLinearModel::generate_risk_factors(RuntimeContext &context,
                                                          sim::ScenarioJournal & /*journal*/) {
    // Per person, then level by level from 1 upwards: a level's factors are predictors for the
    // next, so the order is the model's and the draw order follows it.
    std::map<int, std::vector<MappingEntry>> level_factors;
    for (int level = 1; level <= context.mapping().max_level(); ++level) {
        level_factors.emplace(level, context.mapping().at_level(level));
    }

    for (auto &person : context.population()) {
        for (int level = 1; level <= context.mapping().max_level(); ++level) {
            generate_for_person(context, person, level, level_factors.at(level));
        }
    }
}

void StaticHierarchicalLinearModel::update_risk_factors(RuntimeContext &context,
                                                        sim::ScenarioJournal & /*journal*/) {
    std::map<int, std::vector<MappingEntry>> level_factors;
    for (int level = 1; level <= context.mapping().max_level(); ++level) {
        level_factors.emplace(level, context.mapping().at_level(level));
    }

    for (auto &person : context.population()) {
        // Newborns only: everyone else has last year's values for the dynamic model to move.
        if (!person.is_active() || person.age > 0) {
            continue;
        }

        for (int level = 1; level <= context.mapping().max_level(); ++level) {
            generate_for_person(context, person, level, level_factors.at(level));
        }
    }
}

void StaticHierarchicalLinearModel::generate_for_person(
    RuntimeContext &context, Person &person, int level,
    const std::vector<MappingEntry> &level_factors) const {
    const auto found_level = levels_->find(level);
    if (found_level == levels_->end()) {
        throw diag::InternalError(
            fmt::format("the static model has no definition for level {}", level));
    }
    const auto &level_info = found_level->second;

    if (level_info.residual_distribution.rows() == 0) {
        throw diag::InternalError(
            fmt::format("level {} has an empty residual distribution", level));
    }

    // One residual row per factor, sampled from the empirical distribution.
    //
    // next_int(count) is half-open, so the whole distribution is reachable. The baseline calls
    // next_int(rows - 1) against an inclusive implementation, which reaches the same rows — and
    // would silently miss the last row if that contract were ever corrected (audit B-07).
    std::map<core::Identifier, double> residuals;
    for (const auto &factor : level_factors) {
        const auto row = context.random().next_int(level_info.residual_distribution.rows());
        const auto column = level_info.variables.find(factor.key());
        if (column == level_info.variables.end()) {
            throw diag::InternalError(
                fmt::format("level {} has no column for factor '{}'", level,
                            factor.key().to_string()));
        }
        residuals.emplace(factor.key(), level_info.residual_distribution(row, column->second));
    }

    // The stochastic component: the transition matrix applied to those residuals, each row summed
    // in the level's own column order.
    std::map<core::Identifier, double> stochastic;
    for (const auto &row_factor : level_factors) {
        const auto row = level_info.variables.at(row_factor.key());

        double sum = 0.0;
        for (const auto &column_factor : level_factors) {
            const auto column = level_info.variables.at(column_factor.key());
            sum += level_info.transition(row, column) * residuals.at(column_factor.key());
        }

        stochastic.emplace(row_factor.key(), sum);
    }

    // The deterministic component: each factor's regression on the intercept and on every factor
    // from a lower level.
    std::map<core::Identifier, double> predictors;
    predictors.emplace(kInterceptKey, person.get_risk_factor_value(kInterceptKey));
    for (const auto &entry : context.mapping()) {
        if (entry.level() < level) {
            predictors.emplace(entry.key(), person.get_risk_factor_value(entry.key()));
        }
    }

    for (const auto &factor : level_factors) {
        const auto equation = models_->find(factor.key());
        if (equation == models_->end()) {
            throw diag::InternalError(
                fmt::format("the static model has no equation for factor '{}'",
                            factor.key().to_string()));
        }

        // Ordered by coefficient name, so the sum order is stated.
        double deterministic = 0.0;
        for (const auto &[name, coefficient] : equation->second.coefficients) {
            const auto predictor = predictors.find(name);
            if (predictor == predictors.end()) {
                // A coefficient naming a factor from this level or above cannot be a predictor
                // for it. The baseline reads a default-constructed zero from operator[] here and
                // carries on silently; every coefficient name is validated at load time, so
                // reaching this is a bug in the level assignment.
                throw diag::InternalError(fmt::format(
                    "the equation for '{}' at level {} names predictor '{}', which is not "
                    "available at that level",
                    factor.key().to_string(), level, name.to_string()));
            }
            deterministic += coefficient.value * predictor->second;
        }

        const double total = deterministic + stochastic.at(factor.key());
        person.risk_factors[factor.key()] = factor.get_bounded_value(total);
    }
}

DynamicHierarchicalLinearModel::DynamicHierarchicalLinearModel(
    std::shared_ptr<const SexAgeFactorTable> expected,
    std::shared_ptr<const std::map<core::Identifier, double>> trend,
    std::shared_ptr<const std::map<core::Identifier, int>> trend_steps,
    std::shared_ptr<const std::map<core::IntegerInterval, AgeGroupGenderEquation>> equations,
    std::shared_ptr<const std::map<core::Identifier, core::Identifier>> variables,
    double boundary_percentage)
    : AdjustableRiskFactorModel{std::move(expected), std::move(trend), std::move(trend_steps)},
      equations_{std::move(equations)}, variables_{std::move(variables)},
      boundary_percentage_{boundary_percentage} {
    if (!equations_ || equations_->empty()) {
        throw diag::InternalError("The model equations definition must not be empty");
    }
    if (!variables_ || variables_->empty()) {
        throw diag::InternalError("The model variables definition must not be empty");
    }

    // The factors this model calibrates, in the variables' own order.
    factor_keys_.reserve(variables_->size());
    for (const auto &[variable, factor] : *variables_) {
        factor_keys_.push_back(factor);
    }
}

void DynamicHierarchicalLinearModel::generate_risk_factors(RuntimeContext &context,
                                                           sim::ScenarioJournal &journal) {
    // Nothing to generate: the static model has just produced the values, and this model's job at
    // initialisation is to calibrate them to the expected means.
    adjust_risk_factors(context, journal, factor_keys_, nullptr, false);
}

void DynamicHierarchicalLinearModel::update_risk_factors(RuntimeContext &context,
                                                         sim::ScenarioJournal &journal) {
    static const core::Identifier age_key{"age"};

    for (auto &person : context.population()) {
        // Newborns are the static model's business: there is no previous year to move from.
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        auto current = current_risk_factors(context.mapping(), person);

        // The equations were fitted against last year's age, and the person has already been
        // aged by the demographic module.
        const auto model_age = static_cast<int>(person.age) - 1;
        if (current.contains(age_key) && current.at(age_key) > model_age) {
            current.at(age_key) = model_age;
        }

        const auto &equations = equations_at(model_age);
        update_exposure(context, person, current,
                        person.gender == core::Gender::male ? equations.male : equations.female);
    }

    adjust_risk_factors(context, journal, factor_keys_, nullptr, false);
}

const AgeGroupGenderEquation &DynamicHierarchicalLinearModel::equations_at(int age) const {
    for (const auto &[band, equations] : *equations_) {
        if (band.contains(age)) {
            return equations;
        }
    }

    // Outside every band, the nearest band applies: the model is fitted on the ages it has, and
    // extrapolating with the edge band is what the baseline does.
    if (age < equations_->begin()->first.lower()) {
        return equations_->begin()->second;
    }

    return equations_->rbegin()->second;
}

std::map<core::Identifier, double>
DynamicHierarchicalLinearModel::current_risk_factors(const HierarchicalMapping &mapping,
                                                     const Person &person) {
    std::map<core::Identifier, double> result;
    result.emplace(kInterceptKey, person.get_risk_factor_value(kInterceptKey));
    for (const auto &entry : mapping) {
        result.emplace(entry.key(), person.get_risk_factor_value(entry.key()));
    }
    return result;
}

void DynamicHierarchicalLinearModel::update_exposure(
    RuntimeContext &context, Person &person, const std::map<core::Identifier, double> &current,
    const std::map<core::Identifier, FactorDynamicEquation> &equations) const {
    // The deltas computed so far this year, which later factors' equations refer to through the
    // variables table ("dBMI" means "this year's change in bmi").
    std::map<core::Identifier, double> deltas;

    for (int level = 1; level <= context.mapping().max_level(); ++level) {
        for (const auto &factor : context.mapping().at_level(level)) {
            const auto equation = equations.find(factor.key());
            if (equation == equations.end()) {
                throw diag::InternalError(
                    fmt::format("the dynamic model has no equation for factor '{}'",
                                factor.key().to_string()));
            }

            const double original = person.get_risk_factor_value(factor.key());

            // Ordered by coefficient name: a stated summation order.
            double delta = 0.0;
            for (const auto &[name, coefficient] : equation->second.coefficients) {
                if (const auto value = current.find(name); value != current.end()) {
                    delta += coefficient * value->second;
                    continue;
                }

                // Otherwise the coefficient names a delta variable, which the variables table
                // maps to the factor whose delta it is.
                const auto variable = variables_->find(name);
                if (variable == variables_->end()) {
                    throw diag::InternalError(fmt::format(
                        "the dynamic equation for '{}' names '{}', which is neither a risk factor "
                        "nor a declared delta variable",
                        factor.key().to_string(), name.to_string()));
                }

                const auto computed = deltas.find(variable->second);
                if (computed == deltas.end()) {
                    throw diag::InternalError(fmt::format(
                        "the dynamic equation for '{}' needs the change in '{}', which is "
                        "generated at the same level or later; the model's levels are wrong",
                        factor.key().to_string(), variable->second.to_string()));
                }

                delta += coefficient * computed->second;
            }

            // The intervention applies to last year's value, which is what the equation moves.
            delta = context.scenario().apply(context.random(), person, context.time_now() - 1,
                                             factor.key(), delta);

            delta += sample_normal_with_boundary(
                context.random(), 0.0, equation->second.residuals_standard_deviation, original);

            deltas[factor.key()] = delta;

            const double updated = person.risk_factors.at(factor.key()) + delta;
            person.risk_factors.at(factor.key()) = factor.get_bounded_value(updated);
        }
    }
}

double DynamicHierarchicalLinearModel::sample_normal_with_boundary(rng::RandomSource &random,
                                                                    double mean,
                                                                    double standard_deviation,
                                                                    double boundary) const {
    if (standard_deviation <= 0.0) {
        // A zero residual standard deviation means a deterministic equation, and drawing a normal
        // with it would throw. No draw is taken, so the stream is untouched.
        return mean;
    }

    const double candidate = random.next_normal(mean, standard_deviation);

    // The cap is a share of the factor's magnitude. std::abs matters: for a negative factor the
    // baseline's min/max pair collapses to the negative cap itself, returning a constant instead
    // of a bounded draw. Unreachable with the shipped configs, whose dynamic factors are all
    // positive, and wrong if it ever were reached.
    const double cap = boundary_percentage_ * std::abs(boundary);
    return std::clamp(candidate, -cap, cap);
}

} // namespace hgps::model
