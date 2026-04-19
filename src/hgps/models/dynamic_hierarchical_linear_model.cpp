#include "dynamic_hierarchical_linear_model.h"

#include "hgps_core/diagnostics/internal_error.h"
#include "simulation/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace hgps {

DynamicHierarchicalLinearModel::DynamicHierarchicalLinearModel(
    std::shared_ptr<RiskFactorSexAgeTable> expected,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
    std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
    const std::map<core::IntegerInterval, AgeGroupGenderEquation> &equations,
    const std::map<core::Identifier, core::Identifier> &variables, const double boundary_percentage)
    : RiskFactorAdjustableModel{std::move(expected), std::move(expected_trend),
                                std::move(trend_steps)},
      equations_{equations}, variables_{variables}, boundary_percentage_{boundary_percentage} {
    if (equations_.empty()) {
        throw core::InternalError("The model equations definition must not be empty");
    }
    if (variables_.empty()) {
        throw core::InternalError("The model variables definition must not be empty");
    }
    if (boundary_percentage_ < 0.0) {
        throw core::InternalError("The boundary percentage must not be negative");
    }
}

RiskFactorModelType DynamicHierarchicalLinearModel::type() const noexcept {
    return RiskFactorModelType::Dynamic;
}

std::string DynamicHierarchicalLinearModel::name() const { return "Dynamic"; }

void DynamicHierarchicalLinearModel::generate_risk_factors(RuntimeContext &context) {
    std::vector<core::Identifier> factor_keys;
    factor_keys.reserve(variables_.size());
    for (const auto &[_, factor_name] : variables_) {
        factor_keys.emplace_back(factor_name);
    }

    adjust_risk_factors(context, factor_keys, std::nullopt, false);
}

void DynamicHierarchicalLinearModel::update_risk_factors(RuntimeContext &context) {
    const auto age_key = core::Identifier{"age"};

    for (auto &entity : context.population()) {
        if (!entity.is_active() || entity.age == 0) {
            continue;
        }

        auto current_risk_factors = get_current_risk_factors(context.inputs().risk_mapping(), entity);

        const auto model_age = static_cast<int>(entity.age - 1);
        if (current_risk_factors.at(age_key) > model_age) {
            current_risk_factors.at(age_key) = model_age;
        }

        const auto &equations = equations_at(model_age);
        if (entity.gender == core::Gender::male) {
            update_risk_factors_exposure(context, entity, current_risk_factors, equations.male);
        } else {
            update_risk_factors_exposure(context, entity, current_risk_factors, equations.female);
        }
    }

    std::vector<core::Identifier> factor_keys;
    factor_keys.reserve(variables_.size());
    for (const auto &[_, factor_name] : variables_) {
        factor_keys.emplace_back(factor_name);
    }

    adjust_risk_factors(context, factor_keys, std::nullopt, false);
}

const AgeGroupGenderEquation &DynamicHierarchicalLinearModel::equations_at(const int age) const {
    for (const auto &[interval, equation] : equations_) {
        if (interval.contains(age)) {
            return equation;
        }
    }

    if (age < equations_.begin()->first.lower()) {
        return equations_.begin()->second;
    }

    return equations_.rbegin()->second;
}

void DynamicHierarchicalLinearModel::update_risk_factors_exposure(
    RuntimeContext &context, Person &entity,
    const std::map<core::Identifier, double> &current_risk_factors,
    const std::map<core::Identifier, FactorDynamicEquation> &equations) {
    std::unordered_map<core::Identifier, double> delta_comp_factors;

    for (auto level = 1; level <= context.inputs().risk_mapping().max_level(); ++level) {
        const auto level_factors = context.inputs().risk_mapping().at_level(level);
        for (const auto &factor : level_factors) {
            const auto &factor_equation = equations.at(factor.key());

            const auto original_value = entity.get_risk_factor_value(factor.key());
            double delta_factor = 0.0;

            for (const auto &[coefficient_key, coefficient] : factor_equation.coefficients) {
                if (current_risk_factors.contains(coefficient_key)) {
                    delta_factor += coefficient * current_risk_factors.at(coefficient_key);
                } else {
                    const auto &factor_key = variables_.at(coefficient_key);
                    delta_factor += coefficient * delta_comp_factors.at(factor_key);
                }
            }

            delta_factor = context.scenario().apply(
                context.random(), entity, context.time_now() - 1, factor.key(), delta_factor);

            const auto factor_stdev = factor_equation.residuals_standard_deviation;
            delta_factor +=
                sample_normal_with_boundary(context.random(), 0.0, factor_stdev, original_value);

            delta_comp_factors.emplace(factor.key(), delta_factor);

            const auto updated_value = entity.risk_factors.at(factor.key()) + delta_factor;
            entity.risk_factors.at(factor.key()) = factor.get_bounded_value(updated_value);
        }
    }
}

std::map<core::Identifier, double>
DynamicHierarchicalLinearModel::get_current_risk_factors(const HierarchicalMapping &mapping,
                                                         const Person &entity) {
    auto entity_risk_factors = std::map<core::Identifier, double>{};
    entity_risk_factors.emplace(InterceptKey, entity.get_risk_factor_value(InterceptKey));

    for (const auto &factor : mapping) {
        entity_risk_factors.emplace(factor.key(), entity.get_risk_factor_value(factor.key()));
    }

    return entity_risk_factors;
}

double DynamicHierarchicalLinearModel::sample_normal_with_boundary(
    Random &random, const double mean, const double standard_deviation, const double boundary) const {
    const auto candidate = random.next_normal(mean, standard_deviation);
    const auto cap = boundary_percentage_ * std::abs(boundary);
    return std::clamp(candidate, -cap, cap);
}

DynamicHierarchicalLinearModelDefinition::DynamicHierarchicalLinearModelDefinition(
    std::unique_ptr<RiskFactorSexAgeTable> expected,
    std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
    std::unique_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
    std::map<core::IntegerInterval, AgeGroupGenderEquation> equations,
    std::map<core::Identifier, core::Identifier> variables, const double boundary_percentage)
    : RiskFactorAdjustableModelDefinition{std::move(expected), std::move(expected_trend),
                                          std::move(trend_steps)},
      equations_{std::move(equations)}, variables_{std::move(variables)},
      boundary_percentage_{boundary_percentage} {
    if (equations_.empty()) {
        throw core::InternalError("The model equations definition must not be empty");
    }
    if (variables_.empty()) {
        throw core::InternalError("The model variables definition must not be empty");
    }
    if (boundary_percentage_ < 0.0) {
        throw core::InternalError("The boundary percentage must not be negative");
    }
}

std::unique_ptr<RiskFactorModel> DynamicHierarchicalLinearModelDefinition::create_model() const {
    return std::make_unique<DynamicHierarchicalLinearModel>(
        expected_, expected_trend_, trend_steps_, equations_, variables_, boundary_percentage_);
}

} // namespace hgps