// Years of life lost, years lived with disability, and the disability weights behind them.
#include "analysis_module.h"

#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hgps::model {

AnalysisDefinition::AnalysisDefinition(GenderTable<int, float> life_expectancy,
                                       DoubleAgeGenderTable observed_yld,
                                       std::map<core::Identifier, float> disability_weights)
    : life_expectancy_{std::move(life_expectancy)}, observed_yld_{std::move(observed_yld)},
      disability_weights_{std::move(disability_weights)} {}

double AnalysisModule::calculate_residual_disability_weight(
    int age, core::Gender gender, const DoubleAgeGenderTable &expected_sum,
    const IntegerAgeGenderTable &expected_count) const {
    if (!expected_sum.contains(age) || !definition_.observed_yld().contains(age)) {
        return 0.0;
    }

    const auto count = expected_count.at(age, gender);
    if (count == 0) {
        return 0.0;
    }

    // The disability the modelled diseases do not account for: whatever is left between the
    // observed YLD and the mean of the modelled weights.
    const double expected_mean = expected_sum.at(age, gender) / static_cast<double>(count);
    const double observed_yld = definition_.observed_yld().at(age, gender);
    const double residual = 1.0 - (1.0 - observed_yld) / expected_mean;

    return std::isnan(residual) ? 0.0 : residual;
}

double AnalysisModule::calculate_disability_weight(const Person &person) const {
    // The complement of the product of the complements: disabilities compound rather than add.
    // person.diseases is ordered, so the product is taken in disease-code order.
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

    const double residual =
        std::clamp(residual_disability_weight_.at(static_cast<int>(person.age), person.gender), 0.0,
                   1.0);
    healthy *= 1.0 - residual;

    return 1.0 - healthy;
}

DALYsIndicator AnalysisModule::calculate_dalys(const Population &population, unsigned int max_age,
                                               unsigned int death_year) const {
    double yll_sum = 0.0;
    double yld_sum = 0.0;
    double count = 0.0;

    // Slot order, serially: a sum over the population whose order is the population's.
    for (const auto &person : population) {
        if (person.time_of_death() == death_year && person.age <= max_age) {
            // The reference age is the higher of the two sexes' life expectancies, as upstream.
            const auto male_reference =
                definition_.life_expectancy().at(static_cast<int>(death_year), core::Gender::male);
            const auto female_reference = definition_.life_expectancy().at(
                static_cast<int>(death_year), core::Gender::female);

            const auto reference = std::max(male_reference, female_reference);
            yll_sum += std::max(static_cast<double>(reference) - static_cast<double>(person.age),
                                0.0);
        }

        if (person.is_active()) {
            yld_sum += calculate_disability_weight(person);
            count += 1.0;
        }
    }

    if (count == 0.0) {
        return DALYsIndicator{};
    }

    const double yll = yll_sum * kDalyUnits / count;
    const double yld = yld_sum * kDalyUnits / count;

    return DALYsIndicator{.years_of_life_lost = yll,
                          .years_lived_with_disability = yld,
                          .disability_adjusted_life_years = yll + yld};
}

} // namespace hgps::model
