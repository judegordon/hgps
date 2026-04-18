#include "analysis_tracking.h"

#include "analysis_income.h"
#include "events/individual_tracking_message.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <vector>

namespace hgps::analysis {

void publish_individual_tracking_if_enabled(
    RuntimeContext &context,
    const std::optional<hgps::input::IndividualIdTrackingConfig> &config) {
    if (!config.has_value() || !config->enabled) {
        return;
    }

    const auto &cfg = *config;
    const int current_year = context.time_now();

    if (!cfg.years.empty()) {
        if (std::ranges::find(cfg.years, current_year) == cfg.years.end()) {
            return;
        }
    }

    std::string scenario_lower = context.scenario().name();
    std::ranges::transform(scenario_lower, scenario_lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (cfg.scenarios == "baseline" && scenario_lower != "baseline") {
        return;
    }
    if (cfg.scenarios == "intervention" && scenario_lower != "intervention") {
        return;
    }

    std::vector<std::string> risk_factor_names;
    if (!cfg.risk_factors.empty()) {
        risk_factor_names = cfg.risk_factors;
    } else {
        for (const auto &entry : context.inputs().risk_mapping().entries()) {
            if (entry.level() > 0) {
                risk_factor_names.push_back(entry.name());
            }
        }
    }

    std::vector<IndividualTrackingRow> rows;
    const auto &population = context.population();

    for (const auto &entity : population) {
        if (!entity.is_active()) {
            continue;
        }

        if (cfg.age_min.has_value() && std::cmp_less(entity.age, *cfg.age_min)) {
            continue;
        }
        if (cfg.age_max.has_value() && std::cmp_greater(entity.age, *cfg.age_max)) {
            continue;
        }

        if (cfg.gender != "all") {
            std::string gender_lower = entity.gender_to_string();
            std::ranges::transform(gender_lower, gender_lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (gender_lower != cfg.gender) {
                continue;
            }
        }

        if (!cfg.regions.empty()) {
            if (std::ranges::find(cfg.regions, entity.region) == cfg.regions.end()) {
                continue;
            }
        }

        if (!cfg.ethnicities.empty()) {
            if (std::ranges::find(cfg.ethnicities, entity.ethnicity) == cfg.ethnicities.end()) {
                continue;
            }
        }

        IndividualTrackingRow row{};
        row.id = entity.id();
        row.age = entity.age;
        row.gender = entity.gender_to_string();
        row.region = entity.region;
        row.ethnicity = entity.ethnicity;
        row.income_category = income_category_to_string(entity.income);

        for (const auto &name : risk_factor_names) {
            core::Identifier key(name);
            if (entity.risk_factors.contains(key)) {
                row.risk_factors[name] = entity.risk_factors.at(key);
            }
        }

        rows.push_back(std::move(row));
    }

    if (!rows.empty()) {
        context.publish(std::make_unique<IndividualTrackingEventMessage>(
            context.scenario().name(), context.current_run(), context.time_now(),
            context.scenario().name(), std::move(rows)));
    }
}

} // namespace hgps::analysis