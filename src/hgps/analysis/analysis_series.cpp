#include "analysis_series.h"

#include "analysis_summary.h"

#include "hgps_core/utils/string_util.h"

#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace hgps::analysis {
using hgps::core::operator""_id;

namespace {

bool is_special_series_factor(const core::Identifier &key) {
    const auto lower = core::to_lower(key.to_string());
    return lower == "region" || lower == "ethnicity" || lower == "sector" ||
           lower == "income" || lower == "income_category" ||
           lower == "physicalactivity" || lower == "physical_activity";
}

void add_channel_if_new(std::vector<std::string> &channels, std::unordered_set<std::string> &seen,
                        const std::string &channel_name) {
    const auto lower = core::to_lower(channel_name);
    if (!seen.contains(lower)) {
        channels.emplace_back(channel_name);
        seen.insert(lower);
    }
}

std::unordered_set<std::string> make_channel_set(const std::vector<std::string> &channels) {
    std::unordered_set<std::string> result;
    result.reserve(channels.size());

    for (const auto &channel : channels) {
        result.insert(core::to_lower(channel));
    }

    return result;
}

std::unordered_set<std::string> make_channel_set(const DataSeries &series) {
    std::unordered_set<std::string> result;
    result.reserve(series.channels().size());

    for (const auto &channel : series.channels()) {
        result.insert(core::to_lower(channel));
    }

    return result;
}

} // namespace

void initialise_output_channels(RuntimeContext &context, std::vector<std::string> &channels) {
    if (!channels.empty()) {
        return;
    }

    std::unordered_set<std::string> seen;

    add_channel_if_new(channels, seen, "count");
    add_channel_if_new(channels, seen, "deaths");
    add_channel_if_new(channels, seen, "emigrations");

    add_channel_if_new(channels, seen, "mean_age");
    add_channel_if_new(channels, seen, "std_age");
    add_channel_if_new(channels, seen, "mean_age2");
    add_channel_if_new(channels, seen, "std_age2");
    add_channel_if_new(channels, seen, "mean_age3");
    add_channel_if_new(channels, seen, "std_age3");

    add_channel_if_new(channels, seen, "mean_gender");
    add_channel_if_new(channels, seen, "std_gender");

    add_channel_if_new(channels, seen, "mean_region");
    add_channel_if_new(channels, seen, "std_region");

    add_channel_if_new(channels, seen, "mean_ethnicity");
    add_channel_if_new(channels, seen, "std_ethnicity");

    add_channel_if_new(channels, seen, "mean_sector");
    add_channel_if_new(channels, seen, "std_sector");

    add_channel_if_new(channels, seen, "mean_income");
    add_channel_if_new(channels, seen, "std_income");

    add_channel_if_new(channels, seen, "mean_income_category");
    add_channel_if_new(channels, seen, "std_income_category");

    add_channel_if_new(channels, seen, "mean_physical_activity");
    add_channel_if_new(channels, seen, "std_physical_activity");

    for (const auto &factor : context.inputs().risk_mapping().entries()) {
        if (is_special_series_factor(factor.key())) {
            continue;
        }

        add_channel_if_new(channels, seen, "mean_" + factor.key().to_string());
        add_channel_if_new(channels, seen, "std_" + factor.key().to_string());
    }

    for (const auto &disease : context.inputs().diseases()) {
        add_channel_if_new(channels, seen, "prevalence_" + disease.code.to_string());
        add_channel_if_new(channels, seen, "incidence_" + disease.code.to_string());
    }

    add_channel_if_new(channels, seen, "normal_weight");
    add_channel_if_new(channels, seen, "over_weight");
    add_channel_if_new(channels, seen, "obese_weight");
    add_channel_if_new(channels, seen, "above_weight");

    add_channel_if_new(channels, seen, "mean_yll");
    add_channel_if_new(channels, seen, "std_yll");
    add_channel_if_new(channels, seen, "mean_yld");
    add_channel_if_new(channels, seen, "std_yld");
    add_channel_if_new(channels, seen, "mean_daly");
    add_channel_if_new(channels, seen, "std_daly");
}

void calculate_population_statistics(const AnalysisDefinition &definition,
                                     const WeightModel &weight_classifier,
                                     const DoubleAgeGenderTable &residual_disability_weight,
                                     RuntimeContext &context, DataSeries &series,
                                     const std::vector<std::string> &channels) {
    if (series.size() > 0) {
        throw std::logic_error("This should be a new object!");
    }

    series.add_channels(channels);

    const auto available_channels = make_channel_set(channels);
    const auto current_time = static_cast<unsigned int>(context.time_now());

    auto safe_increment_channel = [&series, &available_channels](core::Gender gender,
                                                                 const std::string &channel,
                                                                 int age) {
        const auto key = core::to_lower(channel);
        if (!available_channels.contains(key)) {
            return;
        }

        series(gender, key).at(age)++;
    };

    auto safe_add_to_channel = [&series, &available_channels](core::Gender gender,
                                                              const std::string &channel, int age,
                                                              double value) {
        const auto key = core::to_lower(channel);
        if (!available_channels.contains(key)) {
            return;
        }

        series(gender, key).at(age) += value;
    };

    for (const auto &person : context.population()) {
        const auto age = person.age;
        const auto gender = person.gender;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                safe_increment_channel(gender, "deaths", age);

                const float expected_life =
                    definition.life_expectancy().at(context.time_now(), gender);
                const double yll = std::max(expected_life - age, 0.0f) * 100'000.0;

                safe_add_to_channel(gender, "mean_yll", age, yll);
                safe_add_to_channel(gender, "mean_daly", age, yll);
            }

            if (person.has_emigrated() && person.time_of_migration() == current_time) {
                safe_increment_channel(gender, "emigrations", age);
            }

            continue;
        }

        safe_increment_channel(gender, "count", age);
        safe_add_to_channel(gender, "mean_gender", age,
                            static_cast<double>(person.gender_to_value()));

        if (!person.region.empty() && person.region != "unknown") {
            safe_add_to_channel(gender, "mean_region", age, person.region_to_value());
        }

        if (!person.ethnicity.empty() && person.ethnicity != "unknown") {
            safe_add_to_channel(gender, "mean_ethnicity", age, person.ethnicity_to_value());
        }

        if (person.sector != core::Sector::unknown) {
            safe_add_to_channel(gender, "mean_sector", age, person.sector_to_value());
        }

        if (person.income != core::Income::unknown) {
            safe_add_to_channel(gender, "mean_income_category", age, person.income_to_value());
        }

        if (person.risk_factors.contains("income"_id)) {
            safe_add_to_channel(gender, "mean_income", age,
                                person.risk_factors.at("income"_id));
        }

        if (const auto pa_it = person.risk_factors.find("PhysicalActivity"_id);
            pa_it != person.risk_factors.end()) {
            safe_add_to_channel(gender, "mean_physical_activity", age, pa_it->second);
        }

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            if (is_special_series_factor(factor.key())) {
                continue;
            }

            if (person.risk_factors.contains(factor.key())) {
                safe_add_to_channel(gender, "mean_" + factor.key().to_string(), age,
                                    person.risk_factors.at(factor.key()));
            }
        }

        for (const auto &[disease_name, disease_state] : person.diseases) {
            if (disease_state.status == DiseaseStatus::active) {
                safe_increment_channel(gender, "prevalence_" + disease_name.to_string(), age);

                if (disease_state.start_time == context.time_now()) {
                    safe_increment_channel(gender, "incidence_" + disease_name.to_string(), age);
                }
            }
        }

        const double dw =
            calculate_disability_weight(definition, residual_disability_weight, person);
        const double yld = dw * 100'000.0;

        safe_add_to_channel(gender, "mean_yld", age, yld);
        safe_add_to_channel(gender, "mean_daly", age, yld);

        classify_weight(weight_classifier, series, person);
    }

    auto safe_divide_channel = [&series, &available_channels](core::Gender gender,
                                                              const std::string &channel_name,
                                                              int age, double count) {
        const auto key = core::to_lower(channel_name);
        if (!available_channels.contains(key)) {
            return;
        }
        if (count <= 0.0) {
            return;
        }

        series(gender, key).at(age) /= count;
    };

    const auto age_range = context.inputs().settings().age_range();

    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        const double count_f = series(core::Gender::female, "count").at(age);
        const double count_m = series(core::Gender::male, "count").at(age);
        const double deaths_f = series(core::Gender::female, "deaths").at(age);
        const double deaths_m = series(core::Gender::male, "deaths").at(age);

        safe_divide_channel(core::Gender::female, "mean_gender", age, count_f);
        safe_divide_channel(core::Gender::male, "mean_gender", age, count_m);

        safe_divide_channel(core::Gender::female, "mean_region", age, count_f);
        safe_divide_channel(core::Gender::male, "mean_region", age, count_m);

        safe_divide_channel(core::Gender::female, "mean_ethnicity", age, count_f);
        safe_divide_channel(core::Gender::male, "mean_ethnicity", age, count_m);

        safe_divide_channel(core::Gender::female, "mean_sector", age, count_f);
        safe_divide_channel(core::Gender::male, "mean_sector", age, count_m);

        safe_divide_channel(core::Gender::female, "mean_income", age, count_f);
        safe_divide_channel(core::Gender::male, "mean_income", age, count_m);

        safe_divide_channel(core::Gender::female, "mean_income_category", age, count_f);
        safe_divide_channel(core::Gender::male, "mean_income_category", age, count_m);

        safe_divide_channel(core::Gender::female, "mean_physical_activity", age, count_f);
        safe_divide_channel(core::Gender::male, "mean_physical_activity", age, count_m);

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            if (is_special_series_factor(factor.key())) {
                continue;
            }

            safe_divide_channel(core::Gender::female, "mean_" + factor.key().to_string(), age,
                                count_f);
            safe_divide_channel(core::Gender::male, "mean_" + factor.key().to_string(), age,
                                count_m);
        }

        for (const auto &disease : context.inputs().diseases()) {
            const std::string prevalence_col = "prevalence_" + disease.code.to_string();
            const std::string incidence_col = "incidence_" + disease.code.to_string();

            safe_divide_channel(core::Gender::female, prevalence_col, age, count_f);
            safe_divide_channel(core::Gender::female, incidence_col, age, count_f);
            safe_divide_channel(core::Gender::male, prevalence_col, age, count_m);
            safe_divide_channel(core::Gender::male, incidence_col, age, count_m);
        }

        safe_divide_channel(core::Gender::female, "mean_yll", age, count_f + deaths_f);
        safe_divide_channel(core::Gender::male, "mean_yll", age, count_m + deaths_m);

        safe_divide_channel(core::Gender::female, "mean_yld", age, count_f);
        safe_divide_channel(core::Gender::male, "mean_yld", age, count_m);

        safe_divide_channel(core::Gender::female, "mean_daly", age, count_f + deaths_f);
        safe_divide_channel(core::Gender::male, "mean_daly", age, count_m + deaths_m);

        series(core::Gender::female, "mean_age").at(age) = static_cast<double>(age);
        series(core::Gender::male, "mean_age").at(age) = static_cast<double>(age);
        series(core::Gender::female, "mean_age2").at(age) = static_cast<double>(age * age);
        series(core::Gender::male, "mean_age2").at(age) = static_cast<double>(age * age);
        series(core::Gender::female, "mean_age3").at(age) =
            static_cast<double>(age * age * age);
        series(core::Gender::male, "mean_age3").at(age) =
            static_cast<double>(age * age * age);
    }
}

void calculate_standard_deviation(const AnalysisDefinition &definition,
                                  const DoubleAgeGenderTable &residual_disability_weight,
                                  RuntimeContext &context, DataSeries &series) {
    const auto available_channels = make_channel_set(series);
    const auto current_time = static_cast<unsigned int>(context.time_now());

    auto accumulate_squared_diffs = [&series, &available_channels](const std::string &chan,
                                                                   core::Gender sex, int age,
                                                                   double value) {
        const std::string mean_channel = core::to_lower("mean_" + chan);
        const std::string std_channel = core::to_lower("std_" + chan);

        if (!available_channels.contains(mean_channel) || !available_channels.contains(std_channel)) {
            return;
        }

        const double mean = series(sex, mean_channel).at(age);
        const double diff = value - mean;
        series(sex, std_channel).at(age) += diff * diff;
    };

    for (const auto &person : context.population()) {
        const unsigned int age = person.age;
        const auto sex = person.gender;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                const float expected_life =
                    definition.life_expectancy().at(context.time_now(), sex);
                const double yll = std::max(expected_life - age, 0.0f) * 100'000.0;

                accumulate_squared_diffs("yll", sex, age, yll);
                accumulate_squared_diffs("daly", sex, age, yll);
            }

            continue;
        }

        const double dw =
            calculate_disability_weight(definition, residual_disability_weight, person);
        const double yld = dw * 100'000.0;

        accumulate_squared_diffs("yld", sex, age, yld);
        accumulate_squared_diffs("daly", sex, age, yld);

        accumulate_squared_diffs("age", sex, age, person.age);
        accumulate_squared_diffs("age2", sex, age, person.age * person.age);
        accumulate_squared_diffs("age3", sex, age, person.age * person.age * person.age);
        accumulate_squared_diffs("gender", sex, age,
                                 static_cast<double>(person.gender_to_value()));

        if (!person.region.empty() && person.region != "unknown") {
            accumulate_squared_diffs("region", sex, age, person.region_to_value());
        }

        if (!person.ethnicity.empty() && person.ethnicity != "unknown") {
            accumulate_squared_diffs("ethnicity", sex, age, person.ethnicity_to_value());
        }

        if (person.sector != core::Sector::unknown) {
            accumulate_squared_diffs("sector", sex, age, person.sector_to_value());
        }

        if (person.income != core::Income::unknown) {
            accumulate_squared_diffs("income_category", sex, age, person.income_to_value());
        }

        if (person.risk_factors.contains("income"_id)) {
            accumulate_squared_diffs("income", sex, age, person.risk_factors.at("income"_id));
        }

        if (const auto pa_it = person.risk_factors.find("PhysicalActivity"_id);
            pa_it != person.risk_factors.end()) {
            accumulate_squared_diffs("physical_activity", sex, age, pa_it->second);
        }

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            if (is_special_series_factor(factor.key())) {
                continue;
            }

            if (person.risk_factors.contains(factor.key())) {
                accumulate_squared_diffs(factor.key().to_string(), sex, age,
                                         person.risk_factors.at(factor.key()));
            }
        }
    }

    auto divide_by_count_sqrt = [&series, &available_channels](const std::string &chan,
                                                               core::Gender sex, int age,
                                                               double count) {
        const std::string std_channel = core::to_lower("std_" + chan);
        if (!available_channels.contains(std_channel)) {
            return;
        }

        if (count > 0.0) {
            const double sum = series(sex, std_channel).at(age);
            series(sex, std_channel).at(age) = std::sqrt(sum / count);
        } else {
            series(sex, std_channel).at(age) = 0.0;
        }
    };

    const auto age_range = context.inputs().settings().age_range();

    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        const double count_f = series(core::Gender::female, "count").at(age);
        const double count_m = series(core::Gender::male, "count").at(age);
        const double deaths_f = series(core::Gender::female, "deaths").at(age);
        const double deaths_m = series(core::Gender::male, "deaths").at(age);

        divide_by_count_sqrt("age", core::Gender::female, age, count_f);
        divide_by_count_sqrt("age", core::Gender::male, age, count_m);

        divide_by_count_sqrt("age2", core::Gender::female, age, count_f);
        divide_by_count_sqrt("age2", core::Gender::male, age, count_m);

        divide_by_count_sqrt("age3", core::Gender::female, age, count_f);
        divide_by_count_sqrt("age3", core::Gender::male, age, count_m);

        divide_by_count_sqrt("gender", core::Gender::female, age, count_f);
        divide_by_count_sqrt("gender", core::Gender::male, age, count_m);

        divide_by_count_sqrt("region", core::Gender::female, age, count_f);
        divide_by_count_sqrt("region", core::Gender::male, age, count_m);

        divide_by_count_sqrt("ethnicity", core::Gender::female, age, count_f);
        divide_by_count_sqrt("ethnicity", core::Gender::male, age, count_m);

        divide_by_count_sqrt("sector", core::Gender::female, age, count_f);
        divide_by_count_sqrt("sector", core::Gender::male, age, count_m);

        divide_by_count_sqrt("income", core::Gender::female, age, count_f);
        divide_by_count_sqrt("income", core::Gender::male, age, count_m);

        divide_by_count_sqrt("income_category", core::Gender::female, age, count_f);
        divide_by_count_sqrt("income_category", core::Gender::male, age, count_m);

        divide_by_count_sqrt("physical_activity", core::Gender::female, age, count_f);
        divide_by_count_sqrt("physical_activity", core::Gender::male, age, count_m);

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            if (is_special_series_factor(factor.key())) {
                continue;
            }

            divide_by_count_sqrt(factor.key().to_string(), core::Gender::female, age, count_f);
            divide_by_count_sqrt(factor.key().to_string(), core::Gender::male, age, count_m);
        }

        divide_by_count_sqrt("yll", core::Gender::female, age, count_f + deaths_f);
        divide_by_count_sqrt("yll", core::Gender::male, age, count_m + deaths_m);

        divide_by_count_sqrt("yld", core::Gender::female, age, count_f);
        divide_by_count_sqrt("yld", core::Gender::male, age, count_m);

        divide_by_count_sqrt("daly", core::Gender::female, age, count_f + deaths_f);
        divide_by_count_sqrt("daly", core::Gender::male, age, count_m + deaths_m);
    }
}

} // namespace hgps::analysis