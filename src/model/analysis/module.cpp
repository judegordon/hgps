// The module's lifecycle and the year's aggregate statistics.
#include "analysis_module.h"

#include "core/parallel.h"
#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hgps::model {

AnalysisModule::AnalysisModule(AnalysisDefinition definition, WeightModel classifier,
                               core::IntegerInterval age_range, unsigned int comorbidities)
    : definition_{std::move(definition)}, classifier_{std::move(classifier)},
      residual_disability_weight_{create_age_gender_table<double>(age_range)},
      comorbidities_{comorbidities} {}

void AnalysisModule::initialise_population(RuntimeContext &context) {
    const auto &age_range = context.age_range();
    const auto &population = context.population();

    // The mean modelled disability weight per age and sex, as a fixed-order reduction. The
    // baseline accumulates it into a shared table under a mutex, inside a parallel loop, having
    // first built a separate is_active vector in another parallel loop (audit N-7).
    struct Accumulator {
        std::map<std::pair<int, core::Gender>, double> sum;
        std::map<std::pair<int, core::Gender>, int> count;
    };

    const auto totals = core::parallel::reduce_ordered(
        population.size(), Accumulator{},
        [this, &population](std::size_t index) {
            Accumulator single;
            const auto &person = population[index];
            if (!person.is_active()) {
                return single;
            }

            double healthy = 1.0;
            for (const auto &[disease, state] : person.diseases) {
                if (state.status != DiseaseStatus::active) {
                    continue;
                }
                const auto weight = definition_.disability_weights().find(disease);
                if (weight != definition_.disability_weights().end()) {
                    healthy *= 1.0 - static_cast<double>(weight->second);
                }
            }

            const auto key = std::make_pair(static_cast<int>(person.age), person.gender);
            single.sum[key] = healthy;
            single.count[key] = 1;
            return single;
        },
        [](Accumulator left, Accumulator right) {
            for (const auto &[key, value] : right.sum) {
                left.sum[key] += value;
            }
            for (const auto &[key, value] : right.count) {
                left.count[key] += value;
            }
            return left;
        });

    auto expected_sum = create_age_gender_table<double>(age_range);
    auto expected_count = create_age_gender_table<int>(age_range);
    for (const auto &[key, value] : totals.sum) {
        const auto [age, gender] = key;
        if (expected_sum.contains(age, gender)) {
            expected_sum.at(age, gender) = value;
            expected_count.at(age, gender) = totals.count.at(key);
        }
    }

    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            residual_disability_weight_.at(age, gender) =
                calculate_residual_disability_weight(age, gender, expected_sum, expected_count);
        }
    }

    initialise_output_channels(context);
}

void AnalysisModule::update_population(RuntimeContext &context) {
    // Nothing to update: a year's analysis is produced by analyse(), which the engine calls and
    // whose result it hands to the writer. The baseline publishes from here onto a concurrent
    // queue, which is what makes its output row order depend on thread scheduling (B-01).
    if (channels_.empty()) {
        initialise_output_channels(context);
    }
}

ModelResult AnalysisModule::analyse(RuntimeContext &context) const {
    const auto age_count = static_cast<std::size_t>(context.age_range().upper()) + 1;

    ModelResult result{age_count};
    calculate_historical_statistics(context, result);
    calculate_population_statistics(context, result.series);

    return result;
}

void AnalysisModule::calculate_historical_statistics(RuntimeContext &context,
                                                     ModelResult &result) const {
    // Every level-1-and-above factor is reported as a mean; level 0 factors are the demographic
    // ones, reported from their own members.
    std::map<core::Identifier, std::map<core::Gender, double>> risk_factors;
    for (const auto &entry : context.mapping()) {
        if (entry.level() > 0) {
            risk_factors.emplace(entry.key(), std::map<core::Gender, double>{});
        }
    }

    std::map<core::Identifier, std::map<core::Gender, int>> prevalence;
    for (const auto &disease : context.diseases()) {
        prevalence.emplace(disease.code, std::map<core::Gender, int>{});
    }

    std::map<unsigned int, ResultByGender> comorbidity;
    for (unsigned int i = 0; i <= comorbidities_; ++i) {
        comorbidity.emplace(i, ResultByGender{});
    }

    std::map<core::Gender, int> age_sum;
    std::map<core::Gender, int> counts;

    const auto analysis_time = static_cast<unsigned int>(context.time_now());
    const auto max_age = static_cast<unsigned int>(context.age_range().upper());

    int dead = 0;
    int migrated = 0;

    const auto &population = context.population();
    for (const auto &person : population) {
        if (!person.is_active()) {
            // A person who left this year is still counted, once, in the year they left.
            if (person.has_emigrated() && person.time_of_migration() == analysis_time) {
                ++migrated;
            }
            if (!person.is_alive() && person.time_of_death() == analysis_time) {
                ++dead;
            }
            continue;
        }

        age_sum[person.gender] += static_cast<int>(person.age);
        counts[person.gender] += 1;

        for (auto &[factor, by_gender] : risk_factors) {
            const auto value = person.risk_factors.find(factor);
            const double factor_value =
                value == person.risk_factors.end() || std::isnan(value->second) ? 0.0
                                                                               : value->second;
            by_gender[person.gender] += factor_value;
        }

        unsigned int active_diseases = 0;
        for (const auto &[disease, state] : person.diseases) {
            if (state.status == DiseaseStatus::active) {
                ++active_diseases;
                const auto counted = prevalence.find(disease);
                if (counted != prevalence.end()) {
                    counted->second[person.gender] += 1;
                }
            }
        }

        comorbidity.at(std::min(active_diseases, comorbidities_)).at(person.gender) += 1;
    }

    // Guarded against zero, so an empty sex reports zeros rather than NaNs.
    const auto males = std::max(1, counts[core::Gender::male]);
    const auto females = std::max(1, counts[core::Gender::female]);

    result.population_size = static_cast<int>(population.size());
    result.number_alive = GenderValue<int>{males, females};
    result.number_dead = dead;
    result.number_emigrated = migrated;
    result.average_age.male = age_sum[core::Gender::male] * 1.0 / males;
    result.average_age.female = age_sum[core::Gender::female] * 1.0 / females;

    for (const auto &[factor, by_gender] : risk_factors) {
        const auto &name = context.mapping().at(factor).name();
        result.risk_factor_average.emplace(
            name, ResultByGender{.male = by_gender.at(core::Gender::male) / males,
                                 .female = by_gender.at(core::Gender::female) / females});
    }

    for (const auto &disease : context.diseases()) {
        result.disease_prevalence.emplace(
            disease.code.to_string(),
            ResultByGender{
                .male = prevalence.at(disease.code).at(core::Gender::male) * 100.0 / males,
                .female = prevalence.at(disease.code).at(core::Gender::female) * 100.0 / females});
    }

    for (const auto &[metric, value] : context.metrics()) {
        result.metrics.emplace(metric, value);
    }

    for (const auto &[number, by_gender] : comorbidity) {
        result.comorbidity.emplace(number,
                                   ResultByGender{.male = by_gender.male * 100.0 / males,
                                                  .female = by_gender.female * 100.0 / females});
    }

    result.indicators = calculate_dalys(population, max_age, analysis_time);

    if (income_analysis_) {
        calculate_income_based_statistics(context, result);
    }
}

} // namespace hgps::model
