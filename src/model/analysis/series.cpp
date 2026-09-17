// The per-age time series: the rows of the result file.
#include "analysis_module.h"

#include "core/string_util.h"
#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kIncome{"income"};
const core::Identifier kPhysicalActivity{"physicalactivity"};

/// Reported from their own members rather than from the factor map, so they must be skipped when
/// the factor map is walked.
bool is_demographic_factor(const std::string &lower_key) {
    return lower_key == "region" || lower_key == "ethnicity" || lower_key == "sector" ||
           lower_key == "income_category" || lower_key == "income";
}

} // namespace

void AnalysisModule::classify_weight(DataSeries &series, const Person &person) const {
    const auto age = static_cast<std::size_t>(person.age);

    switch (classifier_.classify_weight(person)) {
    case WeightCategory::normal:
        series(person.gender, "normal_weight").at(age)++;
        return;
    case WeightCategory::overweight:
        series(person.gender, "over_weight").at(age)++;
        series(person.gender, "above_weight").at(age)++;
        return;
    case WeightCategory::obese:
        series(person.gender, "obese_weight").at(age)++;
        series(person.gender, "above_weight").at(age)++;
        return;
    }

    throw diag::InternalError("unknown weight classification category");
}

void AnalysisModule::calculate_population_statistics(RuntimeContext &context,
                                                     DataSeries &series) const {
    if (!series.channels().empty()) {
        throw diag::InternalError("the result series has already been populated");
    }

    series.add_channels(channels_);

    std::set<std::string> available;
    for (const auto &channel : series.channels()) {
        available.insert(core::to_lower(channel));
    }

    const auto has = [&available](const std::string &channel) {
        return available.contains(core::to_lower(channel));
    };

    const auto accumulate = [&series, &has](core::Gender gender, const std::string &channel,
                                            std::size_t age, double value) {
        if (has(channel)) {
            series(gender, channel).at(age) += value;
        }
    };

    const auto current_time = static_cast<unsigned int>(context.time_now());

    // Slot order, serially: every row of the output is a sum over the population, and doing it in
    // population order is what makes the file reproducible.
    for (const auto &person : context.population()) {
        const auto age = static_cast<std::size_t>(person.age);
        const auto gender = person.gender;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                accumulate(gender, "deaths", age, 1.0);

                const auto expected = definition_.life_expectancy().at(context.time_now(), gender);
                const double yll =
                    std::max(static_cast<double>(expected) - static_cast<double>(person.age), 0.0) *
                    kDalyUnits;
                accumulate(gender, "mean_yll", age, yll);
                accumulate(gender, "mean_daly", age, yll);
            }

            if (person.has_emigrated() && person.time_of_migration() == current_time) {
                accumulate(gender, "emigrations", age, 1.0);
            }

            continue;
        }

        accumulate(gender, "count", age, 1.0);
        accumulate(gender, "mean_gender", age, static_cast<double>(person.gender_to_value()));

        if (person.region != "unknown") {
            accumulate(gender, "mean_region", age, static_cast<double>(person.region_to_value()));
        }
        if (person.ethnicity != "unknown") {
            accumulate(gender, "mean_ethnicity", age,
                       static_cast<double>(person.ethnicity_to_value()));
        }
        if (person.sector != core::Sector::unknown) {
            accumulate(gender, "mean_sector", age, static_cast<double>(person.sector_to_value()));
        }
        if (person.income != core::Income::unknown) {
            accumulate(gender, "mean_income_category", age,
                       static_cast<double>(person.income_to_value()));
        }
        if (const auto income = person.risk_factors.find(kIncome);
            income != person.risk_factors.end()) {
            accumulate(gender, "mean_income", age, income->second);
        }
        if (const auto activity = person.risk_factors.find(kPhysicalActivity);
            activity != person.risk_factors.end()) {
            accumulate(gender, "mean_physical_activity", age, activity->second);
        }

        for (const auto &factor : context.mapping().entries()) {
            const auto key = factor.key().to_string();
            if (is_demographic_factor(key)) {
                continue;
            }
            const auto value = person.risk_factors.find(factor.key());
            if (value != person.risk_factors.end()) {
                accumulate(gender, "mean_" + key, age, value->second);
            }
        }

        for (const auto &[disease, state] : person.diseases) {
            if (state.status != DiseaseStatus::active) {
                continue;
            }
            accumulate(gender, "prevalence_" + disease.to_string(), age, 1.0);
            if (state.start_time == context.time_now()) {
                accumulate(gender, "incidence_" + disease.to_string(), age, 1.0);
            }
        }

        const double yld = calculate_disability_weight(person) * kDalyUnits;
        accumulate(gender, "mean_yld", age, yld);
        accumulate(gender, "mean_daly", age, yld);

        classify_weight(series, person);
    }

    // Sums become means.
    const auto divide = [&series, &has](core::Gender gender, const std::string &channel,
                                        std::size_t age, double count) {
        if (count > 0.0 && has(channel)) {
            series(gender, channel).at(age) /= count;
        }
    };

    const auto age_range = context.age_range();
    for (int age_value = age_range.lower(); age_value <= age_range.upper(); ++age_value) {
        const auto age = static_cast<std::size_t>(age_value);

        const double count_male = series(core::Gender::male, "count").at(age);
        const double count_female = series(core::Gender::female, "count").at(age);
        const double deaths_male = series(core::Gender::male, "deaths").at(age);
        const double deaths_female = series(core::Gender::female, "deaths").at(age);

        for (const auto &factor : context.mapping().entries()) {
            const auto key = factor.key().to_string();
            if (is_demographic_factor(key)) {
                continue;
            }
            divide(core::Gender::male, "mean_" + key, age, count_male);
            divide(core::Gender::female, "mean_" + key, age, count_female);
        }

        for (const auto *channel : {"mean_gender", "mean_region", "mean_ethnicity", "mean_sector",
                                    "mean_income", "mean_income_category",
                                    "mean_physical_activity"}) {
            divide(core::Gender::male, channel, age, count_male);
            divide(core::Gender::female, channel, age, count_female);
        }

        for (const auto &disease : context.diseases()) {
            for (const auto *prefix : {"prevalence_", "incidence_"}) {
                const auto channel = prefix + disease.code.to_string();
                divide(core::Gender::male, channel, age, count_male);
                divide(core::Gender::female, channel, age, count_female);
            }
        }

        // The burden channels are per person-year, so this year's deaths count towards the
        // denominator as well as the living.
        for (const auto *channel : {"mean_yll", "mean_yld", "mean_daly"}) {
            divide(core::Gender::male, channel, age, count_male + deaths_male);
            divide(core::Gender::female, channel, age, count_female + deaths_female);
        }

        // Age is the row's own key, so its mean is exact rather than accumulated.
        const auto age_double = static_cast<double>(age_value);
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            series(gender, "mean_age").at(age) = age_double;
            series(gender, "mean_age2").at(age) = age_double * age_double;
            series(gender, "mean_age3").at(age) = age_double * age_double * age_double;
        }
    }

    calculate_standard_deviation(context, series);

    if (income_analysis_) {
        calculate_income_based_series(context, series);
    }
}

void AnalysisModule::calculate_standard_deviation(RuntimeContext &context,
                                                  DataSeries &series) const {
    std::set<std::string> available;
    for (const auto &channel : series.channels()) {
        available.insert(core::to_lower(channel));
    }

    // The means are already in place, so this is one pass of squared differences.
    const auto accumulate = [&series, &available](const std::string &name, core::Gender gender,
                                                  std::size_t age, double value) {
        if (!available.contains(core::to_lower("mean_" + name)) ||
            !available.contains(core::to_lower("std_" + name))) {
            return;
        }
        const double difference = value - series(gender, "mean_" + name).at(age);
        series(gender, "std_" + name).at(age) += difference * difference;
    };

    const auto current_time = static_cast<unsigned int>(context.time_now());

    for (const auto &person : context.population()) {
        const auto age = static_cast<std::size_t>(person.age);
        const auto gender = person.gender;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                const auto expected = definition_.life_expectancy().at(context.time_now(), gender);
                const double yll =
                    std::max(static_cast<double>(expected) - static_cast<double>(person.age), 0.0) *
                    kDalyUnits;
                accumulate("yll", gender, age, yll);
                accumulate("daly", gender, age, yll);
            }
            continue;
        }

        const double yld = calculate_disability_weight(person) * kDalyUnits;
        accumulate("yld", gender, age, yld);
        accumulate("daly", gender, age, yld);

        const auto age_value = static_cast<double>(person.age);
        accumulate("age", gender, age, age_value);
        accumulate("age2", gender, age, age_value * age_value);
        accumulate("age3", gender, age, age_value * age_value * age_value);
        accumulate("gender", gender, age, static_cast<double>(person.gender_to_value()));

        for (const auto &factor : context.mapping().entries()) {
            const auto key = factor.key().to_string();
            if (is_demographic_factor(key)) {
                continue;
            }
            const auto value = person.risk_factors.find(factor.key());
            if (value != person.risk_factors.end()) {
                accumulate(key, gender, age, value->second);
            }
        }

        if (person.region != "unknown") {
            accumulate("region", gender, age, static_cast<double>(person.region_to_value()));
        }
        if (person.ethnicity != "unknown") {
            accumulate("ethnicity", gender, age, static_cast<double>(person.ethnicity_to_value()));
        }
        if (person.sector != core::Sector::unknown) {
            accumulate("sector", gender, age, static_cast<double>(person.sector_to_value()));
        }
        if (person.income != core::Income::unknown) {
            accumulate("income_category", gender, age,
                       static_cast<double>(person.income_to_value()));
        }
        if (const auto income = person.risk_factors.find(kIncome);
            income != person.risk_factors.end()) {
            accumulate("income", gender, age, income->second);
        }
        if (const auto activity = person.risk_factors.find(kPhysicalActivity);
            activity != person.risk_factors.end()) {
            accumulate("physical_activity", gender, age, activity->second);
        }
    }

    // Sums of squares become standard deviations.
    const auto finish = [&series, &available](const std::string &name, core::Gender gender,
                                              std::size_t age, double count) {
        if (!available.contains(core::to_lower("std_" + name))) {
            return;
        }
        auto &value = series(gender, "std_" + name).at(age);
        value = count > 0.0 ? std::sqrt(value / count) : 0.0;
    };

    const auto age_range = context.age_range();
    for (int age_value = age_range.lower(); age_value <= age_range.upper(); ++age_value) {
        const auto age = static_cast<std::size_t>(age_value);

        const double count_male = series(core::Gender::male, "count").at(age);
        const double count_female = series(core::Gender::female, "count").at(age);
        const double deaths_male = series(core::Gender::male, "deaths").at(age);
        const double deaths_female = series(core::Gender::female, "deaths").at(age);

        for (const auto &factor : context.mapping().entries()) {
            finish(factor.key().to_string(), core::Gender::male, age, count_male);
            finish(factor.key().to_string(), core::Gender::female, age, count_female);
        }

        for (const auto *name : {"age", "age2", "age3", "gender", "region", "ethnicity", "sector",
                                 "income", "income_category", "physical_activity", "yld"}) {
            finish(name, core::Gender::male, age, count_male);
            finish(name, core::Gender::female, age, count_female);
        }

        for (const auto *name : {"yll", "daly"}) {
            finish(name, core::Gender::male, age, count_male + deaths_male);
            finish(name, core::Gender::female, age, count_female + deaths_female);
        }
    }
}

} // namespace hgps::model
