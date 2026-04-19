#include "static_hierarchical_linear_model.h"

#include "hgps_core/diagnostics/internal_error.h"

#include <stdexcept>
#include <utility>

namespace hgps {

StaticHierarchicalLinearModel::StaticHierarchicalLinearModel(
    const std::unordered_map<core::Identifier, LinearModel> &models,
    const std::map<int, HierarchicalLevel> &levels)
    : models_{models}, levels_{levels} {
    if (models_.empty()) {
        throw core::InternalError("The hierarchical model equations definition must not be empty");
    }
    if (levels_.empty()) {
        throw core::InternalError("The hierarchical model levels definition must not be empty");
    }
}

RiskFactorModelType StaticHierarchicalLinearModel::type() const noexcept {
    return RiskFactorModelType::Static;
}

std::string StaticHierarchicalLinearModel::name() const { return "Static"; }

void StaticHierarchicalLinearModel::generate_risk_factors(RuntimeContext &context) {
    std::unordered_map<int, std::vector<MappingEntry>> level_factors_cache;

    for (auto &entity : context.population()) {
        for (auto level = 1; level <= context.inputs().risk_mapping().max_level(); ++level) {
            if (!level_factors_cache.contains(level)) {
                level_factors_cache.emplace(level, context.inputs().risk_mapping().at_level(level));
            }

            generate_for_entity(context, entity, level, level_factors_cache.at(level));
        }
    }
}

void StaticHierarchicalLinearModel::update_risk_factors(RuntimeContext &context) {
    std::unordered_map<int, std::vector<MappingEntry>> level_factors_cache;
    constexpr auto newborn_age = 0u;

    for (auto &entity : context.population()) {
        if (entity.age > newborn_age) {
            continue;
        }

        for (auto level = 1; level <= context.inputs().risk_mapping().max_level(); ++level) {
            if (!level_factors_cache.contains(level)) {
                level_factors_cache.emplace(level, context.inputs().risk_mapping().at_level(level));
            }

            generate_for_entity(context, entity, level, level_factors_cache.at(level));
        }
    }
}

void StaticHierarchicalLinearModel::generate_for_entity(
    RuntimeContext &context, Person &entity, const int level,
    const std::vector<MappingEntry> &level_factors) {
    const auto &level_info = levels_.at(level);

    auto residual_risk_factors = std::map<core::Identifier, double>{};
    for (const auto &factor : level_factors) {
        const auto row_idx =
            context.random().next_int(static_cast<int>(level_info.residual_distribution.rows() - 1));
        const auto col_idx = level_info.variables.at(factor.key());
        residual_risk_factors.emplace(factor.key(),
                                      level_info.residual_distribution(row_idx, col_idx));
    }

    auto stoch_comp_factors = std::map<core::Identifier, double>{};
    for (const auto &factor_row : level_factors) {
        double row_sum = 0.0;
        const auto row_idx = level_info.variables.at(factor_row.key());

        for (const auto &factor_col : level_factors) {
            const auto col_idx = level_info.variables.at(factor_col.key());
            row_sum +=
                level_info.transition(row_idx, col_idx) * residual_risk_factors.at(factor_col.key());
        }

        stoch_comp_factors.emplace(factor_row.key(), row_sum);
    }

    auto determ_risk_factors = std::map<core::Identifier, double>{};
    determ_risk_factors.emplace(InterceptKey, entity.get_risk_factor_value(InterceptKey));
    for (const auto &item : context.inputs().risk_mapping()) {
        if (item.level() < level) {
            determ_risk_factors.emplace(item.key(), entity.get_risk_factor_value(item.key()));
        }
    }

    auto determ_comp_factors = std::map<core::Identifier, double>{};
    for (const auto &factor : level_factors) {
        double sum = 0.0;
        for (const auto &coeff : models_.at(factor.key()).coefficients) {
            sum += coeff.second.value * determ_risk_factors[coeff.first];
        }

        determ_comp_factors.emplace(factor.key(), sum);
    }

    for (const auto &factor : level_factors) {
        const auto total_value =
            determ_comp_factors.at(factor.key()) + stoch_comp_factors.at(factor.key());
        entity.risk_factors[factor.key()] = factor.get_bounded_value(total_value);
    }
}

StaticHierarchicalLinearModelDefinition::StaticHierarchicalLinearModelDefinition(
    std::unordered_map<core::Identifier, LinearModel> linear_models,
    std::map<int, HierarchicalLevel> model_levels)
    : models_{std::move(linear_models)}, levels_{std::move(model_levels)} {
    if (models_.empty()) {
        throw core::InternalError("The hierarchical model equations definition must not be empty");
    }
    if (levels_.empty()) {
        throw core::InternalError("The hierarchical model levels definition must not be empty");
    }
}

std::unique_ptr<RiskFactorModel> StaticHierarchicalLinearModelDefinition::create_model() const {
    return std::make_unique<StaticHierarchicalLinearModel>(models_, levels_);
}

} // namespace hgps