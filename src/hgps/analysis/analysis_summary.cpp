#include "analysis_summary.h"

#include "analysis_income.h"

#include <algorithm>
#include <cmath>

namespace hgps::analysis {

inline constexpr double DALY_UNITS = 100'000.0;

double calculate_disability_weight(const AnalysisDefinition &definition,
                                   const DoubleAgeGenderTable &residual_disability_weight,
                                   const Person &entity) {
    double sum = 1.0;

    for (const auto &disease : entity.diseases) {
        if (disease.second.status == DiseaseStatus::active &&
            definition.disability_weights().contains(disease.first)) {
            sum *= (1.0 - definition.disability_weights().at(disease.first));
        }
    }

    double residual_dw = residual_disability_weight.at(entity.age, entity.gender);
    residual_dw = std::clamp(residual_dw, 0.0, 1.0);
    sum *= (1.0 - residual_dw);

    return 1.0 - sum;
}

DALYsIndicator calculate_dalys(const AnalysisDefinition &definition,
                               const DoubleAgeGenderTable &residual_disability_weight,
                               Population &population, unsigned int max_age,
                               unsigned int death_year) {
    double yll_sum = 0.0;
    double yld_sum = 0.0;
    double count = 0.0;

    for (const auto &entity : population) {
        if (entity.time_of_death() == death_year && entity.age <= max_age) {
            const auto male_reference_age =
                definition.life_expectancy().at(death_year, core::Gender::male);
            const auto female_reference_age =
                definition.life_expectancy().at(death_year, core::Gender::female);

            const auto reference_age = std::max(male_reference_age, female_reference_age);
            const auto life_expectancy_remaining = std::max(reference_age - entity.age, 0.0f);
            yll_sum += life_expectancy_remaining;
        }

        if (entity.is_active()) {
            yld_sum += calculate_disability_weight(definition, residual_disability_weight, entity);
            count++;
        }
    }

    const double yll = yll_sum * DALY_UNITS / count;
    const double yld = yld_sum * DALY_UNITS / count;

    return DALYsIndicator{.years_of_life_lost = yll,
                          .years_lived_with_disability = yld,
                          .disability_adjusted_life_years = yll + yld};
}

void calculate_historical_statistics(const AnalysisDefinition &definition,
                                     const DoubleAgeGenderTable &residual_disability_weight,
                                     unsigned int comorbidities, bool enable_income_analysis,
                                     RuntimeContext &context, ModelResult &result) {
    auto risk_factors = std::map<core::Identifier, std::map<core::Gender, double>>{};
    for (const auto &item : context.inputs().risk_mapping()) {
        if (item.level() > 0) {
            risk_factors.emplace(item.key(), std::map<core::Gender, double>{});
        }
    }

    auto prevalence = std::map<core::Identifier, std::map<core::Gender, int>>{};
    for (const auto &item : context.inputs().diseases()) {
        prevalence.emplace(item.code, std::map<core::Gender, int>{});
    }

    auto comorbidity = std::map<unsigned int, ResultByGender>{};
    for (auto i = 0u; i <= comorbidities; ++i) {
        comorbidity.emplace(i, ResultByGender{});
    }

    auto gender_age_sum = std::map<core::Gender, int>{};
    auto gender_count = std::map<core::Gender, int>{};

    const auto age_upper_bound = context.inputs().settings().age_range().upper();
    const auto analysis_time = static_cast<unsigned int>(context.time_now());

    auto &population = context.population();
    const int population_size = static_cast<int>(population.size());
    int population_dead = 0;
    int population_migrated = 0;

    for (const auto &entity : population) {
        if (!entity.is_active()) {
            if (entity.has_emigrated() && entity.time_of_migration() == analysis_time) {
                population_migrated++;
            }

            if (!entity.is_alive() && entity.time_of_death() == analysis_time) {
                population_dead++;
            }

            continue;
        }

        gender_age_sum[entity.gender] += entity.age;
        gender_count[entity.gender]++;

        for (auto &item : risk_factors) {
            double factor_value = 0.0;
            if (entity.risk_factors.contains(item.first)) {
                factor_value = entity.risk_factors.at(item.first);
                if (std::isnan(factor_value)) {
                    factor_value = 0.0;
                }
            }

            item.second[entity.gender] += factor_value;
        }

        unsigned int comorbidity_number = 0;
        for (const auto &item : entity.diseases) {
            if (item.second.status == DiseaseStatus::active) {
                comorbidity_number++;
                prevalence.at(item.first)[entity.gender]++;
            }
        }

        if (comorbidity_number > comorbidities) {
            comorbidity_number = comorbidities;
        }

        if (entity.gender == core::Gender::male) {
            comorbidity[comorbidity_number].male++;
        } else {
            comorbidity[comorbidity_number].female++;
        }
    }

    const auto dalys =
        calculate_dalys(definition, residual_disability_weight, population, age_upper_bound,
                        analysis_time);

    const auto males_count = std::max(1, gender_count[core::Gender::male]);
    const auto females_count = std::max(1, gender_count[core::Gender::female]);

    result.population_size = population_size;
    result.number_alive = IntegerGenderValue{males_count, females_count};
    result.number_dead = population_dead;
    result.number_emigrated = population_migrated;
    result.average_age.male = gender_age_sum[core::Gender::male] * 1.0 / males_count;
    result.average_age.female = gender_age_sum[core::Gender::female] * 1.0 / females_count;

    for (auto &item : risk_factors) {
        const auto user_name = context.inputs().risk_mapping().at(item.first).name();
        result.risk_factor_average.emplace(
            user_name, ResultByGender{.male = item.second[core::Gender::male] / males_count,
                                      .female = item.second[core::Gender::female] / females_count});
    }

    for (const auto &item : context.inputs().diseases()) {
        result.disease_prevalence.emplace(
            item.code.to_string(),
            ResultByGender{
                .male = prevalence.at(item.code)[core::Gender::male] * 100.0 / males_count,
                .female = prevalence.at(item.code)[core::Gender::female] * 100.0 / females_count});
    }

    for (const auto &item : context.metrics()) {
        result.metrics.emplace(item.first, item.second);
    }

    for (const auto &item : comorbidity) {
        result.comorbidity.emplace(
            item.first, ResultByGender{.male = item.second.male * 100.0 / males_count,
                                       .female = item.second.female * 100.0 / females_count});
    }

    result.indicators = dalys;

    if (enable_income_analysis) {
        calculate_income_based_statistics(definition, residual_disability_weight, comorbidities,
                                          context, result);
    }
}

void classify_weight(const WeightModel &weight_classifier, DataSeries &series,
                     const Person &entity) {
    switch (weight_classifier.classify_weight(entity)) {
    case WeightCategory::normal:
        series(entity.gender, "normal_weight").at(entity.age)++;
        break;
    case WeightCategory::overweight:
        series(entity.gender, "over_weight").at(entity.age)++;
        series(entity.gender, "above_weight").at(entity.age)++;
        break;
    case WeightCategory::obese:
        series(entity.gender, "obese_weight").at(entity.age)++;
        series(entity.gender, "above_weight").at(entity.age)++;
        break;
    default:
        throw std::logic_error("Unknown weight classification category.");
    }
}

} // namespace hgps::analysis