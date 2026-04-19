#include "analysis_income.h"
#include "analysis_summary.h"

#include "hgps_core/utils/string_util.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace hgps::analysis {
using hgps::core::operator""_id;

namespace {

bool is_special_income_factor(const core::Identifier &key) {
    const auto lower = core::to_lower(key.to_string());
    return lower == "region" || lower == "ethnicity" || lower == "sector" ||
           lower == "income" || lower == "income_category" ||
           lower == "physicalactivity" || lower == "physical_activity";
}

void add_channel_if_new(std::vector<std::string> &channels, std::unordered_set<std::string> &seen,
                        const std::string &channel) {
    const auto lower = core::to_lower(channel);
    if (!seen.contains(lower)) {
        channels.emplace_back(channel);
        seen.insert(lower);
    }
}

std::vector<std::string> build_income_output_channels(RuntimeContext &context) {
    std::vector<std::string> channels;
    std::unordered_set<std::string> seen;

    add_channel_if_new(channels, seen, "count");
    add_channel_if_new(channels, seen, "deaths");
    add_channel_if_new(channels, seen, "emigrations");

    add_channel_if_new(channels, seen, "mean_gender");
    add_channel_if_new(channels, seen, "std_gender");

    add_channel_if_new(channels, seen, "mean_income");
    add_channel_if_new(channels, seen, "std_income");

    add_channel_if_new(channels, seen, "mean_region");
    add_channel_if_new(channels, seen, "std_region");

    add_channel_if_new(channels, seen, "mean_ethnicity");
    add_channel_if_new(channels, seen, "std_ethnicity");

    add_channel_if_new(channels, seen, "mean_sector");
    add_channel_if_new(channels, seen, "std_sector");

    add_channel_if_new(channels, seen, "mean_income_category");
    add_channel_if_new(channels, seen, "std_income_category");

    add_channel_if_new(channels, seen, "mean_physical_activity");
    add_channel_if_new(channels, seen, "std_physical_activity");

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

    for (const auto &factor : context.inputs().risk_mapping().entries()) {
        if (is_special_income_factor(factor.key())) {
            continue;
        }

        add_channel_if_new(channels, seen, "mean_" + factor.key().to_string());
        add_channel_if_new(channels, seen, "std_" + factor.key().to_string());
    }

    for (const auto &disease : context.inputs().diseases()) {
        add_channel_if_new(channels, seen, "prevalence_" + disease.code.to_string());
        add_channel_if_new(channels, seen, "incidence_" + disease.code.to_string());
    }

    return channels;
}

void set_income_gender_value(ResultByIncomeGender &target, core::Income income, double male,
                             double female) {
    switch (income) {
    case core::Income::low:
        target.low = ResultByGender{male, female};
        break;
    case core::Income::lowermiddle:
        target.lowermiddle = ResultByGender{male, female};
        break;
    case core::Income::middle:
        target.middle = ResultByGender{male, female};
        break;
    case core::Income::uppermiddle:
        target.uppermiddle = ResultByGender{male, female};
        break;
    case core::Income::high:
        target.high = ResultByGender{male, female};
        break;
    default:
        break;
    }
}

void set_income_count_value(ResultByIncome &target, core::Income income, int value) {
    switch (income) {
    case core::Income::low:
        target.low = value;
        break;
    case core::Income::lowermiddle:
        target.lowermiddle = value;
        break;
    case core::Income::middle:
        target.middle = value;
        break;
    case core::Income::uppermiddle:
        target.uppermiddle = value;
        break;
    case core::Income::high:
        target.high = value;
        break;
    default:
        break;
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

} // namespace

std::vector<core::Income> get_available_income_categories(RuntimeContext &context) {
    std::vector<core::Income> categories;
    std::unordered_set<core::Income> seen;

    for (const auto &person : context.population()) {
        if (!seen.contains(person.income)) {
            categories.emplace_back(person.income);
            seen.insert(person.income);
        }
    }

    return categories;
}

std::string income_category_to_string(core::Income income) {
    switch (income) {
    case core::Income::low:
        return "LowIncome";
    case core::Income::lowermiddle:
        return "LowerMiddleIncome";
    case core::Income::middle:
        return "MiddleIncome";
    case core::Income::uppermiddle:
        return "UpperMiddleIncome";
    case core::Income::high:
        return "HighIncome";
    case core::Income::unknown:
        return "UnknownIncome";
    default:
        return "UnknownIncome";
    }
}

void calculate_income_based_statistics(const AnalysisDefinition &definition,
                                       const DoubleAgeGenderTable &residual_disability_weight,
                                       unsigned int comorbidities, RuntimeContext &context,
                                       ModelResult &result) {
    (void)definition;
    (void)residual_disability_weight;

    auto risk_factors_by_income =
        std::map<core::Identifier, std::map<core::Income, std::map<core::Gender, double>>>();
    auto prevalence_by_income =
        std::map<core::Identifier, std::map<core::Income, std::map<core::Gender, int>>>();
    auto comorbidity_by_income = std::map<unsigned int, std::map<core::Income, ResultByGender>>();
    auto population_by_income = std::map<core::Income, int>();
    auto population_by_income_and_gender =
        std::map<core::Income, std::map<core::Gender, int>>();

    for (const auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }

        const auto income = person.income;
        const auto gender = person.gender;

        population_by_income[income]++;
        population_by_income_and_gender[income][gender]++;

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            if (factor.level() > 0 && person.risk_factors.contains(factor.key())) {
                risk_factors_by_income[factor.key()][income][gender] +=
                    person.risk_factors.at(factor.key());
            }
        }

        unsigned int active_comorbidity_count = 0;
        for (const auto &[disease_name, disease_state] : person.diseases) {
            if (disease_state.status == DiseaseStatus::active) {
                prevalence_by_income[disease_name][income][gender]++;
                active_comorbidity_count++;
            }
        }

        active_comorbidity_count = std::min(active_comorbidity_count, comorbidities);
        if (gender == core::Gender::male) {
            comorbidity_by_income[active_comorbidity_count][income].male++;
        } else {
            comorbidity_by_income[active_comorbidity_count][income].female++;
        }
    }

    result.population_by_income = ResultByIncome{};
    result.risk_factor_average_by_income = std::map<std::string, ResultByIncomeGender>{};
    result.disease_prevalence_by_income = std::map<std::string, ResultByIncomeGender>{};
    result.comorbidity_by_income = std::map<unsigned int, ResultByIncomeGender>{};

    for (const auto &[income, count] : population_by_income) {
        set_income_count_value(*result.population_by_income, income, count);
    }

    for (const auto &[factor_key, income_data] : risk_factors_by_income) {
        const auto factor_name = context.inputs().risk_mapping().at(factor_key).name();
        auto result_by_income = ResultByIncomeGender{};

        for (const auto &[income, gender_data] : income_data) {
            const double male_count =
                population_by_income_and_gender[income][core::Gender::male];
            const double female_count =
                population_by_income_and_gender[income][core::Gender::female];

            double male_avg = 0.0;
            double female_avg = 0.0;

            if (auto male_it = gender_data.find(core::Gender::male);
                male_it != gender_data.end() && male_count > 0.0) {
                male_avg = male_it->second / male_count;
            }

            if (auto female_it = gender_data.find(core::Gender::female);
                female_it != gender_data.end() && female_count > 0.0) {
                female_avg = female_it->second / female_count;
            }

            set_income_gender_value(result_by_income, income, male_avg, female_avg);
        }

        result.risk_factor_average_by_income->emplace(factor_name, result_by_income);
    }

    for (const auto &[disease_key, income_data] : prevalence_by_income) {
        const auto disease_name = disease_key.to_string();
        auto result_by_income = ResultByIncomeGender{};

        for (const auto &[income, gender_data] : income_data) {
            const double male_count =
                population_by_income_and_gender[income][core::Gender::male];
            const double female_count =
                population_by_income_and_gender[income][core::Gender::female];

            double male_prevalence = 0.0;
            double female_prevalence = 0.0;

            if (auto male_it = gender_data.find(core::Gender::male);
                male_it != gender_data.end() && male_count > 0.0) {
                male_prevalence = male_it->second * 100.0 / male_count;
            }

            if (auto female_it = gender_data.find(core::Gender::female);
                female_it != gender_data.end() && female_count > 0.0) {
                female_prevalence = female_it->second * 100.0 / female_count;
            }

            set_income_gender_value(result_by_income, income, male_prevalence, female_prevalence);
        }

        result.disease_prevalence_by_income->emplace(disease_name, result_by_income);
    }

    for (const auto &[comorbidity_count, income_data] : comorbidity_by_income) {
        auto result_by_income = ResultByIncomeGender{};

        for (const auto &[income, gender_data] : income_data) {
            const double male_count =
                population_by_income_and_gender[income][core::Gender::male];
            const double female_count =
                population_by_income_and_gender[income][core::Gender::female];

            const double male_pct =
                male_count > 0.0 ? gender_data.male * 100.0 / male_count : 0.0;
            const double female_pct =
                female_count > 0.0 ? gender_data.female * 100.0 / female_count : 0.0;

            set_income_gender_value(result_by_income, income, male_pct, female_pct);
        }

        result.comorbidity_by_income->emplace(comorbidity_count, result_by_income);
    }
}

void calculate_income_based_population_statistics(
    const AnalysisDefinition &definition, const WeightModel &weight_classifier,
    const DoubleAgeGenderTable &residual_disability_weight, RuntimeContext &context,
    DataSeries &series) {
    const auto income_channels = build_income_output_channels(context);
    const auto income_categories = get_available_income_categories(context);

    series.add_income_channels_for_categories(income_channels, income_categories);

    const auto available_channels = make_channel_set(income_channels);

    auto safe_increment_channel = [&series, &available_channels](core::Gender gender,
                                                                 core::Income income,
                                                                 const std::string &channel,
                                                                 int age) {
        const auto key = core::to_lower(channel);
        if (!series.has_income_category(gender, income)) {
            return;
        }
        if (!available_channels.contains(key)) {
            return;
        }

        series.at(gender, income, key).at(age)++;
    };

    auto safe_add_to_channel = [&series, &available_channels](core::Gender gender,
                                                              core::Income income,
                                                              const std::string &channel, int age,
                                                              double value) {
        const auto key = core::to_lower(channel);
        if (!series.has_income_category(gender, income)) {
            return;
        }
        if (!available_channels.contains(key)) {
            return;
        }

        series.at(gender, income, key).at(age) += value;
    };

    const auto current_time = static_cast<unsigned int>(context.time_now());

    for (const auto &person : context.population()) {
        const auto age = person.age;
        const auto gender = person.gender;
        const auto income = person.income;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                safe_increment_channel(gender, income, "deaths", age);

                const float expected_life =
                    definition.life_expectancy().at(context.time_now(), gender);
                const double yll = std::max(expected_life - age, 0.0f) * 100'000.0;

                safe_add_to_channel(gender, income, "mean_yll", age, yll);
                safe_add_to_channel(gender, income, "mean_daly", age, yll);
            }

            if (person.has_emigrated() && person.time_of_migration() == current_time) {
                safe_increment_channel(gender, income, "emigrations", age);
            }

            continue;
        }

        safe_increment_channel(gender, income, "count", age);
        safe_add_to_channel(gender, income, "mean_gender", age,
                            static_cast<double>(person.gender_to_value()));

        if (!person.region.empty() && person.region != "unknown") {
            safe_add_to_channel(gender, income, "mean_region", age, person.region_to_value());
        }

        if (!person.ethnicity.empty() && person.ethnicity != "unknown") {
            safe_add_to_channel(gender, income, "mean_ethnicity", age,
                                person.ethnicity_to_value());
        }

        if (person.sector != core::Sector::unknown) {
            safe_add_to_channel(gender, income, "mean_sector", age, person.sector_to_value());
        }

        if (person.income != core::Income::unknown) {
            safe_add_to_channel(gender, income, "mean_income_category", age,
                                person.income_to_value());
        }

        if (person.risk_factors.contains("income"_id)) {
            safe_add_to_channel(gender, income, "mean_income", age,
                                person.risk_factors.at("income"_id));
        }

        if (const auto pa_it = person.risk_factors.find("PhysicalActivity"_id);
            pa_it != person.risk_factors.end()) {
            safe_add_to_channel(gender, income, "mean_physical_activity", age, pa_it->second);
        }

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            if (is_special_income_factor(factor.key())) {
                continue;
            }

            if (person.risk_factors.contains(factor.key())) {
                safe_add_to_channel(gender, income, "mean_" + factor.key().to_string(), age,
                                    person.risk_factors.at(factor.key()));
            }
        }

        for (const auto &[disease_name, disease_state] : person.diseases) {
            if (disease_state.status == DiseaseStatus::active) {
                safe_increment_channel(gender, income,
                                       "prevalence_" + disease_name.to_string(), age);

                if (disease_state.start_time == context.time_now()) {
                    safe_increment_channel(gender, income,
                                           "incidence_" + disease_name.to_string(), age);
                }
            }
        }

        const double dw =
            calculate_disability_weight(definition, residual_disability_weight, person);
        const double yld = dw * 100'000.0;

        safe_add_to_channel(gender, income, "mean_yld", age, yld);
        safe_add_to_channel(gender, income, "mean_daly", age, yld);

        switch (weight_classifier.classify_weight(person)) {
        case WeightCategory::normal:
            safe_increment_channel(gender, income, "normal_weight", age);
            break;
        case WeightCategory::overweight:
            safe_increment_channel(gender, income, "over_weight", age);
            safe_increment_channel(gender, income, "above_weight", age);
            break;
        case WeightCategory::obese:
            safe_increment_channel(gender, income, "obese_weight", age);
            safe_increment_channel(gender, income, "above_weight", age);
            break;
        default:
            break;
        }
    }

    auto safe_divide_channel = [&series, &available_channels](core::Gender gender,
                                                              core::Income income,
                                                              const std::string &channel_name,
                                                              int age, double count) {
        const auto key = core::to_lower(channel_name);
        if (!series.has_income_category(gender, income)) {
            return;
        }
        if (!available_channels.contains(key)) {
            return;
        }
        if (count <= 0.0) {
            return;
        }

        series.at(gender, income, key).at(age) /= count;
    };

    const auto age_range = context.inputs().settings().age_range();

    for (const auto income : income_categories) {
        for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
            const double count_f = series.at(core::Gender::female, income, "count").at(age);
            const double count_m = series.at(core::Gender::male, income, "count").at(age);
            const double deaths_f = series.at(core::Gender::female, income, "deaths").at(age);
            const double deaths_m = series.at(core::Gender::male, income, "deaths").at(age);

            safe_divide_channel(core::Gender::female, income, "mean_gender", age, count_f);
            safe_divide_channel(core::Gender::male, income, "mean_gender", age, count_m);

            safe_divide_channel(core::Gender::female, income, "mean_region", age, count_f);
            safe_divide_channel(core::Gender::male, income, "mean_region", age, count_m);

            safe_divide_channel(core::Gender::female, income, "mean_ethnicity", age, count_f);
            safe_divide_channel(core::Gender::male, income, "mean_ethnicity", age, count_m);

            safe_divide_channel(core::Gender::female, income, "mean_sector", age, count_f);
            safe_divide_channel(core::Gender::male, income, "mean_sector", age, count_m);

            safe_divide_channel(core::Gender::female, income, "mean_income", age, count_f);
            safe_divide_channel(core::Gender::male, income, "mean_income", age, count_m);

            safe_divide_channel(core::Gender::female, income, "mean_income_category", age,
                                count_f);
            safe_divide_channel(core::Gender::male, income, "mean_income_category", age, count_m);

            safe_divide_channel(core::Gender::female, income, "mean_physical_activity", age,
                                count_f);
            safe_divide_channel(core::Gender::male, income, "mean_physical_activity", age,
                                count_m);

            for (const auto &factor : context.inputs().risk_mapping().entries()) {
                if (is_special_income_factor(factor.key())) {
                    continue;
                }

                safe_divide_channel(core::Gender::female, income,
                                    "mean_" + factor.key().to_string(), age, count_f);
                safe_divide_channel(core::Gender::male, income,
                                    "mean_" + factor.key().to_string(), age, count_m);
            }

            for (const auto &disease : context.inputs().diseases()) {
                const auto prevalence_col = "prevalence_" + disease.code.to_string();
                const auto incidence_col = "incidence_" + disease.code.to_string();

                safe_divide_channel(core::Gender::female, income, prevalence_col, age, count_f);
                safe_divide_channel(core::Gender::male, income, prevalence_col, age, count_m);

                safe_divide_channel(core::Gender::female, income, incidence_col, age, count_f);
                safe_divide_channel(core::Gender::male, income, incidence_col, age, count_m);
            }

            safe_divide_channel(core::Gender::female, income, "mean_yll", age,
                                count_f + deaths_f);
            safe_divide_channel(core::Gender::male, income, "mean_yll", age, count_m + deaths_m);

            safe_divide_channel(core::Gender::female, income, "mean_yld", age, count_f);
            safe_divide_channel(core::Gender::male, income, "mean_yld", age, count_m);

            safe_divide_channel(core::Gender::female, income, "mean_daly", age,
                                count_f + deaths_f);
            safe_divide_channel(core::Gender::male, income, "mean_daly", age, count_m + deaths_m);
        }
    }

    calculate_income_based_standard_deviation(definition, residual_disability_weight, context,
                                              series);
}

void calculate_income_based_standard_deviation(
    const AnalysisDefinition &definition, const DoubleAgeGenderTable &residual_disability_weight,
    RuntimeContext &context, DataSeries &series) {
    const auto available_channels = make_channel_set(build_income_output_channels(context));

    auto accumulate_squared_diffs = [&series, &available_channels](const std::string &chan,
                                                                   core::Gender gender,
                                                                   core::Income income, int age,
                                                                   double value) {
        const auto mean_channel = core::to_lower("mean_" + chan);
        const auto std_channel = core::to_lower("std_" + chan);

        if (!series.has_income_category(gender, income)) {
            return;
        }
        if (!available_channels.contains(mean_channel) || !available_channels.contains(std_channel)) {
            return;
        }

        const double mean = series.at(gender, income, mean_channel).at(age);
        const double diff = value - mean;
        series.at(gender, income, std_channel).at(age) += diff * diff;
    };

    const auto current_time = static_cast<unsigned int>(context.time_now());

    for (const auto &person : context.population()) {
        const auto age = person.age;
        const auto gender = person.gender;
        const auto income = person.income;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                const float expected_life =
                    definition.life_expectancy().at(context.time_now(), gender);
                const double yll = std::max(expected_life - age, 0.0f) * 100'000.0;

                accumulate_squared_diffs("yll", gender, income, age, yll);
                accumulate_squared_diffs("daly", gender, income, age, yll);
            }

            continue;
        }

        accumulate_squared_diffs("gender", gender, income, age,
                                 static_cast<double>(person.gender_to_value()));

        if (!person.region.empty() && person.region != "unknown") {
            accumulate_squared_diffs("region", gender, income, age, person.region_to_value());
        }

        if (!person.ethnicity.empty() && person.ethnicity != "unknown") {
            accumulate_squared_diffs("ethnicity", gender, income, age,
                                     person.ethnicity_to_value());
        }

        if (person.sector != core::Sector::unknown) {
            accumulate_squared_diffs("sector", gender, income, age, person.sector_to_value());
        }

        if (person.income != core::Income::unknown) {
            accumulate_squared_diffs("income_category", gender, income, age,
                                     person.income_to_value());
        }

        if (person.risk_factors.contains("income"_id)) {
            accumulate_squared_diffs("income", gender, income, age,
                                     person.risk_factors.at("income"_id));
        }

        if (const auto pa_it = person.risk_factors.find("PhysicalActivity"_id);
            pa_it != person.risk_factors.end()) {
            accumulate_squared_diffs("physical_activity", gender, income, age, pa_it->second);
        }

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            if (is_special_income_factor(factor.key())) {
                continue;
            }

            if (person.risk_factors.contains(factor.key())) {
                accumulate_squared_diffs(factor.key().to_string(), gender, income, age,
                                         person.risk_factors.at(factor.key()));
            }
        }

        const double dw =
            calculate_disability_weight(definition, residual_disability_weight, person);
        const double yld = dw * 100'000.0;

        accumulate_squared_diffs("yld", gender, income, age, yld);
        accumulate_squared_diffs("daly", gender, income, age, yld);
    }

    auto divide_by_count_sqrt = [&series, &available_channels](const std::string &chan,
                                                               core::Gender gender,
                                                               core::Income income, int age,
                                                               double count) {
        const auto std_channel = core::to_lower("std_" + chan);

        if (!series.has_income_category(gender, income)) {
            return;
        }
        if (!available_channels.contains(std_channel)) {
            return;
        }

        if (count > 0.0) {
            const double sum = series.at(gender, income, std_channel).at(age);
            series.at(gender, income, std_channel).at(age) = std::sqrt(sum / count);
        } else {
            series.at(gender, income, std_channel).at(age) = 0.0;
        }
    };

    const auto age_range = context.inputs().settings().age_range();
    const auto income_categories = get_available_income_categories(context);

    for (const auto income : income_categories) {
        for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
            const double count_f = series.at(core::Gender::female, income, "count").at(age);
            const double count_m = series.at(core::Gender::male, income, "count").at(age);
            const double deaths_f = series.at(core::Gender::female, income, "deaths").at(age);
            const double deaths_m = series.at(core::Gender::male, income, "deaths").at(age);

            divide_by_count_sqrt("gender", core::Gender::female, income, age, count_f);
            divide_by_count_sqrt("gender", core::Gender::male, income, age, count_m);

            divide_by_count_sqrt("region", core::Gender::female, income, age, count_f);
            divide_by_count_sqrt("region", core::Gender::male, income, age, count_m);

            divide_by_count_sqrt("ethnicity", core::Gender::female, income, age, count_f);
            divide_by_count_sqrt("ethnicity", core::Gender::male, income, age, count_m);

            divide_by_count_sqrt("sector", core::Gender::female, income, age, count_f);
            divide_by_count_sqrt("sector", core::Gender::male, income, age, count_m);

            divide_by_count_sqrt("income", core::Gender::female, income, age, count_f);
            divide_by_count_sqrt("income", core::Gender::male, income, age, count_m);

            divide_by_count_sqrt("income_category", core::Gender::female, income, age, count_f);
            divide_by_count_sqrt("income_category", core::Gender::male, income, age, count_m);

            divide_by_count_sqrt("physical_activity", core::Gender::female, income, age, count_f);
            divide_by_count_sqrt("physical_activity", core::Gender::male, income, age, count_m);

            for (const auto &factor : context.inputs().risk_mapping().entries()) {
                if (is_special_income_factor(factor.key())) {
                    continue;
                }

                divide_by_count_sqrt(factor.key().to_string(), core::Gender::female, income, age,
                                     count_f);
                divide_by_count_sqrt(factor.key().to_string(), core::Gender::male, income, age,
                                     count_m);
            }

            divide_by_count_sqrt("yll", core::Gender::female, income, age, count_f + deaths_f);
            divide_by_count_sqrt("yll", core::Gender::male, income, age, count_m + deaths_m);

            divide_by_count_sqrt("yld", core::Gender::female, income, age, count_f);
            divide_by_count_sqrt("yld", core::Gender::male, income, age, count_m);

            divide_by_count_sqrt("daly", core::Gender::female, income, age, count_f + deaths_f);
            divide_by_count_sqrt("daly", core::Gender::male, income, age, count_m + deaths_m);
        }
    }
}

} // namespace hgps::analysis