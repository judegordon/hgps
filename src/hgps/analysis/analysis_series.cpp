#include "analysis_series.h"

#include "analysis_summary.h"

#include "hgps_core/utils/string_util.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace hgps::analysis {

void initialise_output_channels(RuntimeContext &context, std::vector<std::string> &channels) {
    if (!channels.empty()) {
        return;
    }

    std::unordered_set<std::string> added_channels;

    auto add_channel_if_new = [&channels, &added_channels](const std::string &channel_name) {
        const auto lower_channel = core::to_lower(channel_name);
        if (!added_channels.contains(lower_channel)) {
            channels.emplace_back(channel_name);
            added_channels.insert(lower_channel);
        }
    };

    add_channel_if_new("count");
    add_channel_if_new("deaths");
    add_channel_if_new("emigrations");

    add_channel_if_new("mean_age");
    add_channel_if_new("std_age");
    add_channel_if_new("mean_age2");
    add_channel_if_new("std_age2");
    add_channel_if_new("mean_age3");
    add_channel_if_new("std_age3");
    add_channel_if_new("mean_gender");
    add_channel_if_new("std_gender");

    bool has_region = false;
    bool has_ethnicity = false;
    bool has_sector = false;
    bool has_income_category = false;
    bool has_income = false;
    bool has_physical_activity = false;

    const auto &pop = context.population();
    const size_t sample_size = std::min(pop.size(), static_cast<size_t>(1000));
    for (size_t i = 0; i < sample_size; ++i) {
        const auto &person = pop[i];
        if (!person.is_active()) {
            continue;
        }

        if (!person.region.empty() && person.region != "unknown") {
            has_region = true;
        }
        if (!person.ethnicity.empty() && person.ethnicity != "unknown") {
            has_ethnicity = true;
        }
        if (person.sector != core::Sector::unknown) {
            has_sector = true;
        }
        if (person.income != core::Income::unknown) {
            has_income_category = true;
        }
        if (person.risk_factors.contains("income"_id)) {
            has_income = true;
        }
        if (person.risk_factors.contains("PhysicalActivity"_id)) {
            has_physical_activity = true;
        }

        if (has_region && has_ethnicity && has_sector && has_income_category && has_income &&
            has_physical_activity) {
            break;
        }
    }

    if (has_region) {
        add_channel_if_new("mean_region");
        add_channel_if_new("std_region");
    }
    if (has_ethnicity) {
        add_channel_if_new("mean_ethnicity");
        add_channel_if_new("std_ethnicity");
    }
    if (has_sector) {
        add_channel_if_new("mean_sector");
        add_channel_if_new("std_sector");
    }
    if (has_income_category) {
        add_channel_if_new("mean_income_category");
        add_channel_if_new("std_income_category");
    }
    if (has_income) {
        add_channel_if_new("mean_income");
        add_channel_if_new("std_income");
    }
    if (has_physical_activity) {
        add_channel_if_new("mean_physical_activity");
        add_channel_if_new("std_physical_activity");
    }

    for (const auto &factor : context.inputs().risk_mapping().entries()) {
        add_channel_if_new("mean_" + factor.key().to_string());
        add_channel_if_new("std_" + factor.key().to_string());
    }

    for (const auto &disease : context.inputs().diseases()) {
        add_channel_if_new("prevalence_" + disease.code.to_string());
        add_channel_if_new("incidence_" + disease.code.to_string());
    }

    add_channel_if_new("normal_weight");
    add_channel_if_new("over_weight");
    add_channel_if_new("obese_weight");
    add_channel_if_new("above_weight");
    add_channel_if_new("mean_yll");
    add_channel_if_new("std_yll");
    add_channel_if_new("mean_yld");
    add_channel_if_new("std_yld");
    add_channel_if_new("mean_daly");
    add_channel_if_new("std_daly");
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

    const auto current_time = static_cast<unsigned int>(context.time_now());
    const auto &pop = context.population();

    for (const auto &person : pop) {
        const auto age = person.age;
        const auto gender = person.gender;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                series(gender, "deaths").at(age)++;
                const float expected_life = definition.life_expectancy().at(context.time_now(), gender);
                const double yll = std::max(expected_life - age, 0.0f) * 100'000.0;
                series(gender, "mean_yll").at(age) += yll;
                series(gender, "mean_daly").at(age) += yll;
            }

            if (person.has_emigrated() && person.time_of_migration() == current_time) {
                series(gender, "emigrations").at(age)++;
            }

            continue;
        }

        series(gender, "count").at(age)++;
        series(gender, "mean_gender").at(age) += static_cast<double>(person.gender_to_value());

        if (!person.region.empty() && person.region != "unknown") {
            series(gender, "mean_region").at(age) += person.region_to_value();
        }
        if (!person.ethnicity.empty() && person.ethnicity != "unknown") {
            series(gender, "mean_ethnicity").at(age) += person.ethnicity_to_value();
        }
        if (person.sector != core::Sector::unknown) {
            series(gender, "mean_sector").at(age) += person.sector_to_value();
        }
        if (person.income != core::Income::unknown) {
            series(gender, "mean_income_category").at(age) += person.income_to_value();
        }
        if (person.risk_factors.contains("income"_id)) {
            series(gender, "mean_income").at(age) += person.risk_factors.at("income"_id);
        }

        if (const auto pa_it = person.risk_factors.find("PhysicalActivity"_id);
            pa_it != person.risk_factors.end()) {
            series(gender, "mean_physical_activity").at(age) += pa_it->second;
        }

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            const auto key_str = factor.key().to_string();
            const auto key_lower = core::to_lower(key_str);

            if (key_lower == "region" || key_lower == "ethnicity" || key_lower == "sector" ||
                key_lower == "income_category" || key_lower == "income") {
                continue;
            }

            if (person.risk_factors.contains(factor.key())) {
                series(gender, core::to_lower("mean_" + key_str)).at(age) +=
                    person.risk_factors.at(factor.key());
            }
        }

        for (const auto &[disease_name, disease_state] : person.diseases) {
            if (disease_state.status == DiseaseStatus::active) {
                series(gender, "prevalence_" + disease_name.to_string()).at(age)++;
                if (disease_state.start_time == context.time_now()) {
                    series(gender, "incidence_" + disease_name.to_string()).at(age)++;
                }
            }
        }

        const double dw = calculate_disability_weight(definition, residual_disability_weight, person);
        const double yld = dw * 100'000.0;
        series(gender, "mean_yld").at(age) += yld;
        series(gender, "mean_daly").at(age) += yld;

        classify_weight(weight_classifier, series, person);
    }

    const auto age_range = context.inputs().settings().age_range();

    std::unordered_set<std::string> available_channels;
    for (const auto &channel : series.channels()) {
        available_channels.insert(core::to_lower(channel));
    }

    auto safe_divide_channel = [&series, &available_channels](core::Gender gender,
                                                              const std::string &channel_name,
                                                              int age, double count) {
        const auto channel_key = core::to_lower(channel_name);
        if (count > 0 && available_channels.contains(channel_key)) {
            series(gender, channel_name).at(age) /= count;
        }
    };

    const std::unordered_set<std::string> demographic_mean_channels = {
        "mean_gender", "mean_region", "mean_ethnicity",
        "mean_sector", "mean_income", "mean_income_category"};

    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        const double count_f = series(core::Gender::female, "count").at(age);
        const double count_m = series(core::Gender::male, "count").at(age);
        const double deaths_f = series(core::Gender::female, "deaths").at(age);
        const double deaths_m = series(core::Gender::male, "deaths").at(age);

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            const std::string column = "mean_" + factor.key().to_string();
            if (demographic_mean_channels.contains(core::to_lower(column))) {
                continue;
            }
            safe_divide_channel(core::Gender::female, column, age, count_f);
            safe_divide_channel(core::Gender::male, column, age, count_m);
        }

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

        for (const auto &disease : context.inputs().diseases()) {
            const std::string prevalence_col = "prevalence_" + disease.code.to_string();
            const std::string incidence_col = "incidence_" + disease.code.to_string();
            safe_divide_channel(core::Gender::female, prevalence_col, age, count_f);
            safe_divide_channel(core::Gender::female, incidence_col, age, count_f);
            safe_divide_channel(core::Gender::male, prevalence_col, age, count_m);
            safe_divide_channel(core::Gender::male, incidence_col, age, count_m);
        }

        for (const auto &column : {"mean_yll", "mean_yld", "mean_daly"}) {
            safe_divide_channel(core::Gender::female, column, age, count_f + deaths_f);
            safe_divide_channel(core::Gender::male, column, age, count_m + deaths_m);
        }

        series(core::Gender::female, "mean_age").at(age) = static_cast<double>(age);
        series(core::Gender::male, "mean_age").at(age) = static_cast<double>(age);
        series(core::Gender::female, "mean_age2").at(age) = static_cast<double>(age * age);
        series(core::Gender::male, "mean_age2").at(age) = static_cast<double>(age * age);
        series(core::Gender::female, "mean_age3").at(age) = static_cast<double>(age * age * age);
        series(core::Gender::male, "mean_age3").at(age) = static_cast<double>(age * age * age);
    }
}

void calculate_standard_deviation(const AnalysisDefinition &definition,
                                  const DoubleAgeGenderTable &residual_disability_weight,
                                  RuntimeContext &context, DataSeries &series) {
    std::unordered_set<std::string> available_channels;
    for (const auto &channel : series.channels()) {
        available_channels.insert(core::to_lower(channel));
    }

    auto accumulate_squared_diffs = [&series, &available_channels](const std::string &chan,
                                                                   core::Gender sex, int age,
                                                                   double value) {
        const std::string mean_channel = core::to_lower("mean_" + chan);
        const std::string std_channel = core::to_lower("std_" + chan);

        if (available_channels.contains(mean_channel) && available_channels.contains(std_channel)) {
            const double mean = series(sex, "mean_" + chan).at(age);
            const double diff = value - mean;
            series(sex, "std_" + chan).at(age) += diff * diff;
        }
    };

    const auto current_time = static_cast<unsigned int>(context.time_now());
    const auto &pop = context.population();

    for (const auto &person : pop) {
        const unsigned int age = person.age;
        const auto sex = person.gender;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                const float expected_life = definition.life_expectancy().at(context.time_now(), sex);
                const double yll = std::max(expected_life - age, 0.0f) * 100'000.0;
                accumulate_squared_diffs("yll", sex, age, yll);
                accumulate_squared_diffs("daly", sex, age, yll);
            }
            continue;
        }

        const double dw = calculate_disability_weight(definition, residual_disability_weight, person);
        const double yld = dw * 100'000.0;
        accumulate_squared_diffs("yld", sex, age, yld);
        accumulate_squared_diffs("daly", sex, age, yld);

        accumulate_squared_diffs("age", sex, age, person.age);
        accumulate_squared_diffs("age2", sex, age, person.age * person.age);
        accumulate_squared_diffs("age3", sex, age, person.age * person.age * person.age);
        accumulate_squared_diffs("gender", sex, age, static_cast<double>(person.gender_to_value()));

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            const auto key_str = factor.key().to_string();

            if (key_str == "income_category") {
                if (person.income != core::Income::unknown) {
                    accumulate_squared_diffs("income_category", sex, age, person.income_to_value());
                }
                continue;
            }

            if (person.risk_factors.contains(factor.key())) {
                accumulate_squared_diffs(key_str, sex, age, person.risk_factors.at(factor.key()));
            }
        }

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
        if (const auto pa_it = person.risk_factors.find("PhysicalActivity"_id);
            pa_it != person.risk_factors.end()) {
            accumulate_squared_diffs("physical_activity", sex, age, pa_it->second);
        }
    }

    auto divide_by_count_sqrt = [&series, &available_channels](const std::string &chan,
                                                               core::Gender sex, int age,
                                                               double count) {
        const std::string std_channel = core::to_lower("std_" + chan);
        if (!available_channels.contains(std_channel)) {
            return;
        }

        if (count > 0) {
            const double sum = series(sex, "std_" + chan).at(age);
            series(sex, "std_" + chan).at(age) = std::sqrt(sum / count);
        } else {
            series(sex, "std_" + chan).at(age) = 0.0;
        }
    };

    const auto age_range = context.inputs().settings().age_range();

    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        const double count_f = series(core::Gender::female, "count").at(age);
        const double count_m = series(core::Gender::male, "count").at(age);
        const double deaths_f = series(core::Gender::female, "deaths").at(age);
        const double deaths_m = series(core::Gender::male, "deaths").at(age);

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            divide_by_count_sqrt(factor.key().to_string(), core::Gender::female, age, count_f);
            divide_by_count_sqrt(factor.key().to_string(), core::Gender::male, age, count_m);
        }

        divide_by_count_sqrt("age", core::Gender::female, age, count_f);
        divide_by_count_sqrt("age", core::Gender::male, age, count_m);
        divide_by_count_sqrt("age2", core::Gender::female, age, count_f);
        divide_by_count_sqrt("age2", core::Gender::male, age, count_m);
        divide_by_count_sqrt("age3", core::Gender::female, age, count_f);
        divide_by_count_sqrt("age3", core::Gender::male, age, count_m);
        divide_by_count_sqrt("gender", core::Gender::female, age, count_f);
        divide_by_count_sqrt("gender", core::Gender::male, age, count_m);

        for (const auto &column : {"yll", "yld", "daly"}) {
            divide_by_count_sqrt(column, core::Gender::female, age, count_f + deaths_f);
            divide_by_count_sqrt(column, core::Gender::male, age, count_m + deaths_m);
        }

        divide_by_count_sqrt("region", core::Gender::female, age, count_f);
        divide_by_count_sqrt("region", core::Gender::male, age, count_m);
        divide_by_count_sqrt("ethnicity", core::Gender::female, age, count_f);
        divide_by_count_sqrt("ethnicity", core::Gender::male, age, count_m);
        divide_by_count_sqrt("sector", core::Gender::female, age, count_f);
        divide_by_count_sqrt("sector", core::Gender::male, age, count_m);
        divide_by_count_sqrt("income_category", core::Gender::female, age, count_f);
        divide_by_count_sqrt("income_category", core::Gender::male, age, count_m);
        divide_by_count_sqrt("physical_activity", core::Gender::female, age, count_f);
        divide_by_count_sqrt("physical_activity", core::Gender::male, age, count_m);
    }
}

} // namespace hgps::analysis