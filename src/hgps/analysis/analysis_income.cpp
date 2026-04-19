#include "analysis_income.h"
#include "analysis_summary.h"

#include <algorithm>
#include <cmath>
#include <oneapi/tbb/parallel_for_each.h>
#include <ranges>
#include <unordered_set>

namespace hgps::analysis {
    using hgps::core::operator""_id;

std::vector<core::Income> get_available_income_categories(RuntimeContext &context) {
    std::vector<core::Income> categories;
    std::unordered_set<core::Income> seen;

    const auto &pop = context.population();
    for (const auto &person : pop) {
        if (!person.is_active()) {
            continue;
        }

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
    auto risk_factors_by_income =
        std::map<core::Identifier, std::map<core::Income, std::map<core::Gender, double>>>();
    auto prevalence_by_income =
        std::map<core::Identifier, std::map<core::Income, std::map<core::Gender, int>>>();
    auto comorbidity_by_income = std::map<unsigned int, std::map<core::Income, ResultByGender>>();
    auto population_by_income = std::map<core::Income, int>();

    const auto &pop = context.population();
    for (const auto &person : pop) {
        if (!person.is_active()) {
            continue;
        }

        const auto income = person.income;
        population_by_income[income]++;

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            if (factor.level() > 0 && person.risk_factors.contains(factor.key())) {
                risk_factors_by_income[factor.key()][income][person.gender] +=
                    person.risk_factors.at(factor.key());
            }
        }

        unsigned int active_comorbidity_count = 0;
        for (const auto &[disease_name, disease_state] : person.diseases) {
            if (disease_state.status == DiseaseStatus::active) {
                prevalence_by_income[disease_name][income][person.gender]++;
                active_comorbidity_count++;
            }
        }

        active_comorbidity_count = std::min(active_comorbidity_count, comorbidities);
        if (person.gender == core::Gender::male) {
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
        switch (income) {
        case core::Income::low:
            result.population_by_income->low = count;
            break;
        case core::Income::lowermiddle:
            result.population_by_income->lowermiddle = count;
            break;
        case core::Income::middle:
            result.population_by_income->middle = count;
            break;
        case core::Income::uppermiddle:
            result.population_by_income->uppermiddle = count;
            break;
        case core::Income::high:
            result.population_by_income->high = count;
            break;
        default:
            break;
        }
    }

    for (const auto &[factor_key, income_data] : risk_factors_by_income) {
        auto factor_name = context.inputs().risk_mapping().at(factor_key).name();
        auto result_by_income = ResultByIncomeGender{};

        for (const auto &[income, gender_data] : income_data) {
            const auto count = population_by_income[income];
            if (count <= 0) {
                continue;
            }

            double male_avg = 0.0;
            double female_avg = 0.0;

            if (auto male_it = gender_data.find(core::Gender::male); male_it != gender_data.end()) {
                male_avg = male_it->second / count;
            }
            if (auto female_it = gender_data.find(core::Gender::female);
                female_it != gender_data.end()) {
                female_avg = female_it->second / count;
            }

            switch (income) {
            case core::Income::low:
                result_by_income.low = ResultByGender{male_avg, female_avg};
                break;
            case core::Income::lowermiddle:
                result_by_income.lowermiddle = ResultByGender{male_avg, female_avg};
                break;
            case core::Income::middle:
                result_by_income.middle = ResultByGender{male_avg, female_avg};
                break;
            case core::Income::uppermiddle:
                result_by_income.uppermiddle = ResultByGender{male_avg, female_avg};
                break;
            case core::Income::high:
                result_by_income.high = ResultByGender{male_avg, female_avg};
                break;
            default:
                break;
            }
        }

        result.risk_factor_average_by_income->emplace(factor_name, result_by_income);
    }

    for (const auto &[disease_key, income_data] : prevalence_by_income) {
        auto disease_name = disease_key.to_string();
        auto result_by_income = ResultByIncomeGender{};

        for (const auto &[income, gender_data] : income_data) {
            const auto count = population_by_income[income];
            if (count <= 0) {
                continue;
            }

            double male_prevalence = 0.0;
            double female_prevalence = 0.0;

            if (auto male_it = gender_data.find(core::Gender::male); male_it != gender_data.end()) {
                male_prevalence = male_it->second * 100.0 / count;
            }
            if (auto female_it = gender_data.find(core::Gender::female);
                female_it != gender_data.end()) {
                female_prevalence = female_it->second * 100.0 / count;
            }

            switch (income) {
            case core::Income::low:
                result_by_income.low = ResultByGender{male_prevalence, female_prevalence};
                break;
            case core::Income::lowermiddle:
                result_by_income.lowermiddle =
                    ResultByGender{male_prevalence, female_prevalence};
                break;
            case core::Income::middle:
                result_by_income.middle = ResultByGender{male_prevalence, female_prevalence};
                break;
            case core::Income::uppermiddle:
                result_by_income.uppermiddle =
                    ResultByGender{male_prevalence, female_prevalence};
                break;
            case core::Income::high:
                result_by_income.high = ResultByGender{male_prevalence, female_prevalence};
                break;
            default:
                break;
            }
        }

        result.disease_prevalence_by_income->emplace(disease_name, result_by_income);
    }

    for (const auto &[comorbidity_count, income_data] : comorbidity_by_income) {
        auto result_by_income = ResultByIncomeGender{};

        for (const auto &[income, gender_data] : income_data) {
            const auto count = population_by_income[income];
            if (count <= 0) {
                continue;
            }

            const double male_comorbidity = gender_data.male * 100.0 / count;
            const double female_comorbidity = gender_data.female * 100.0 / count;

            switch (income) {
            case core::Income::low:
                result_by_income.low = ResultByGender{male_comorbidity, female_comorbidity};
                break;
            case core::Income::lowermiddle:
                result_by_income.lowermiddle =
                    ResultByGender{male_comorbidity, female_comorbidity};
                break;
            case core::Income::middle:
                result_by_income.middle = ResultByGender{male_comorbidity, female_comorbidity};
                break;
            case core::Income::uppermiddle:
                result_by_income.uppermiddle =
                    ResultByGender{male_comorbidity, female_comorbidity};
                break;
            case core::Income::high:
                result_by_income.high = ResultByGender{male_comorbidity, female_comorbidity};
                break;
            default:
                break;
            }
        }

        result.comorbidity_by_income->emplace(comorbidity_count, result_by_income);
    }
}

void calculate_income_based_population_statistics(
    const AnalysisDefinition &definition, const WeightModel &weight_classifier,
    const DoubleAgeGenderTable &residual_disability_weight, RuntimeContext &context,
    DataSeries &series) {
    auto income_channels = std::vector<std::string>{
        "count",       "deaths",      "emigrations", "mean_yll",      "mean_yld",
        "mean_daly",   "normal_weight", "over_weight", "obese_weight", "above_weight",  "mean_gender", "std_gender",  "mean_income",   "std_income"};

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

    if (has_sector) {
        income_channels.emplace_back("mean_sector");
        income_channels.emplace_back("std_sector");
    }
    if (has_region) {
        income_channels.emplace_back("mean_region");
        income_channels.emplace_back("std_region");
    }
    if (has_ethnicity) {
        income_channels.emplace_back("mean_ethnicity");
        income_channels.emplace_back("std_ethnicity");
    }
    if (has_income_category) {
        income_channels.emplace_back("mean_income_category");
        income_channels.emplace_back("std_income_category");
    }
    if (has_physical_activity) {
        income_channels.emplace_back("mean_physical_activity");
        income_channels.emplace_back("std_physical_activity");
    }

    for (const auto &factor : context.inputs().risk_mapping().entries()) {
        income_channels.emplace_back("mean_" + factor.key().to_string());
        income_channels.emplace_back("std_" + factor.key().to_string());
    }

    for (const auto &disease : context.inputs().diseases()) {
        income_channels.emplace_back("prevalence_" + disease.code.to_string());
        income_channels.emplace_back("incidence_" + disease.code.to_string());
    }

    income_channels.emplace_back("std_yll");
    income_channels.emplace_back("std_yld");
    income_channels.emplace_back("std_daly");

    std::vector<core::Income> actual_income_categories;
    for (const auto &person : context.population()) {
        if (std::ranges::find(actual_income_categories, person.income) ==
            actual_income_categories.end()) {
            actual_income_categories.emplace_back(person.income);
        }
    }

    series.add_income_channels_for_categories(income_channels, actual_income_categories);

    const auto age_range = context.inputs().settings().age_range();

    std::unordered_set<std::string> added_income_channels;
    for (const auto &channel : income_channels) {
        added_income_channels.insert(core::to_lower(channel));
    }

    auto safe_init_channel = [&series, &added_income_channels](core::Gender gender,
                                                               core::Income income,
                                                               const std::string &channel, int age) {
        const std::string channel_key = core::to_lower(channel);
        if (added_income_channels.contains(channel_key)) {
            series.at(gender, income, channel_key).at(age) = 0.0;
        }
    };

    for (const auto &income : actual_income_categories) {
        for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
            for (const auto &factor : context.inputs().risk_mapping().entries()) {
                safe_init_channel(core::Gender::female, income,
                                  "std_" + factor.key().to_string(), age);
                safe_init_channel(core::Gender::male, income,
                                  "std_" + factor.key().to_string(), age);
            }

            for (const auto &column : {"std_yll", "std_yld", "std_daly", "std_income",
                                       "std_sector", "std_age2", "std_age3", "std_region",
                                       "std_ethnicity", "std_income_category",
                                       "std_physical_activity"}) {
                safe_init_channel(core::Gender::female, income, column, age);
                safe_init_channel(core::Gender::male, income, column, age);
            }
        }
    }

    auto safe_add_to_channel = [&series, &added_income_channels](core::Gender gender,
                                                                 core::Income income,
                                                                 const std::string &channel,
                                                                 int age, double value) {
        const std::string channel_key = core::to_lower(channel);
        if (added_income_channels.contains(channel_key)) {
            series.at(gender, income, channel_key).at(age) += value;
        }
    };

    auto safe_increment_channel = [&series, &added_income_channels](core::Gender gender,
                                                                    core::Income income,
                                                                    const std::string &channel,
                                                                    int age) {
        const std::string channel_key = core::to_lower(channel);
        if (added_income_channels.contains(channel_key)) {
            series.at(gender, income, channel_key).at(age)++;
        }
    };

    const auto current_time = static_cast<unsigned int>(context.time_now());

    for (const auto &person : pop) {
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

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            const auto key_str = factor.key().to_string();

            if (key_str == "income_category") {
                if (person.income != core::Income::unknown) {
                    safe_add_to_channel(gender, income, "mean_income_category", age,
                                        person.income_to_value());
                }
                continue;
            }

            if (key_str == "income" || key_str == "Income") {
                continue;
            }

            if (person.risk_factors.contains(factor.key())) {
                safe_add_to_channel(gender, income, "mean_" + key_str, age,
                                    person.risk_factors.at(factor.key()));
            }
        }

        if (person.risk_factors.contains("income"_id)) {
            safe_add_to_channel(gender, income, "mean_income", age,
                                person.risk_factors.at("income"_id));
        }
        if (person.sector != core::Sector::unknown) {
            safe_add_to_channel(gender, income, "mean_sector", age, person.sector_to_value());
        }
        if (!person.region.empty() && person.region != "unknown") {
            safe_add_to_channel(gender, income, "mean_region", age, person.region_to_value());
        }
        if (!person.ethnicity.empty() && person.ethnicity != "unknown") {
            safe_add_to_channel(gender, income, "mean_ethnicity", age,
                                person.ethnicity_to_value());
        }
        if (person.income != core::Income::unknown) {
            safe_add_to_channel(gender, income, "mean_income_category", age,
                                person.income_to_value());
        }
        if (auto pa_it = person.risk_factors.find("PhysicalActivity"_id);
            pa_it != person.risk_factors.end()) {
            safe_add_to_channel(gender, income, "mean_physical_activity", age, pa_it->second);
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

    auto safe_divide_channel_income = [&series, &added_income_channels](
                                          core::Gender gender, core::Income income,
                                          const std::string &channel, int age, double count) {
        const std::string channel_key = core::to_lower(channel);
        if (count > 0 && added_income_channels.contains(channel_key)) {
            series.at(gender, income, channel_key).at(age) /= count;
        }
    };

    const std::unordered_set<std::string> income_demo_mean_channels = {
        "mean_gender", "mean_region", "mean_ethnicity",
        "mean_sector", "mean_income", "mean_income_category"};

    const auto available_income_categories = get_available_income_categories(context);

    for (const auto &income : available_income_categories) {
        for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
            const double count_f = series.at(core::Gender::female, income, "count").at(age);
            const double count_m = series.at(core::Gender::male, income, "count").at(age);
            const double deaths_f = series.at(core::Gender::female, income, "deaths").at(age);
            const double deaths_m = series.at(core::Gender::male, income, "deaths").at(age);

            for (const auto &factor : context.inputs().risk_mapping().entries()) {
                const std::string column = "mean_" + factor.key().to_string();
                if (income_demo_mean_channels.contains(core::to_lower(column))) {
                    continue;
                }
                safe_divide_channel_income(core::Gender::female, income, column, age, count_f);
                safe_divide_channel_income(core::Gender::male, income, column, age, count_m);
            }

            safe_divide_channel_income(core::Gender::female, income, "mean_gender", age, count_f);
            safe_divide_channel_income(core::Gender::male, income, "mean_gender", age, count_m);
            safe_divide_channel_income(core::Gender::female, income, "mean_income", age, count_f);
            safe_divide_channel_income(core::Gender::male, income, "mean_income", age, count_m);
            safe_divide_channel_income(core::Gender::female, income, "mean_sector", age, count_f);
            safe_divide_channel_income(core::Gender::male, income, "mean_sector", age, count_m);
            safe_divide_channel_income(core::Gender::female, income, "mean_region", age, count_f);
            safe_divide_channel_income(core::Gender::male, income, "mean_region", age, count_m);
            safe_divide_channel_income(core::Gender::female, income, "mean_ethnicity", age,
                                       count_f);
            safe_divide_channel_income(core::Gender::male, income, "mean_ethnicity", age, count_m);
            safe_divide_channel_income(core::Gender::female, income, "mean_income_category", age,
                                       count_f);
            safe_divide_channel_income(core::Gender::male, income, "mean_income_category", age,
                                       count_m);
            safe_divide_channel_income(core::Gender::female, income, "mean_physical_activity", age,
                                       count_f);
            safe_divide_channel_income(core::Gender::male, income, "mean_physical_activity", age,
                                       count_m);

            for (const auto &disease : context.inputs().diseases()) {
                const std::string prevalence_col =
                    "prevalence_" + disease.code.to_string();
                const std::string incidence_col =
                    "incidence_" + disease.code.to_string();

                safe_divide_channel_income(core::Gender::female, income, prevalence_col, age,
                                           count_f);
                safe_divide_channel_income(core::Gender::male, income, prevalence_col, age, count_m);
                safe_divide_channel_income(core::Gender::female, income, incidence_col, age,
                                           count_f);
                safe_divide_channel_income(core::Gender::male, income, incidence_col, age, count_m);
            }

            for (const auto &column : {"mean_yll", "mean_yld", "mean_daly"}) {
                safe_divide_channel_income(core::Gender::female, income, column, age,
                                           count_f + deaths_f);
                safe_divide_channel_income(core::Gender::male, income, column, age,
                                           count_m + deaths_m);
            }
        }
    }

    calculate_income_based_standard_deviation(definition, residual_disability_weight, context,
                                              series);
}

void calculate_income_based_standard_deviation(
    const AnalysisDefinition &definition, const DoubleAgeGenderTable &residual_disability_weight,
    RuntimeContext &context, DataSeries &series) {
    auto income_channels = std::vector<std::string>{
        "count",       "deaths",      "emigrations", "mean_yll",      "mean_yld",
        "mean_daly",   "normal_weight", "over_weight", "obese_weight", "above_weight",
          "mean_gender", "std_gender",  "mean_income",   "std_income"};

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

    if (has_sector) {
        income_channels.emplace_back("mean_sector");
        income_channels.emplace_back("std_sector");
    }
    if (has_region) {
        income_channels.emplace_back("mean_region");
        income_channels.emplace_back("std_region");
    }
    if (has_ethnicity) {
        income_channels.emplace_back("mean_ethnicity");
        income_channels.emplace_back("std_ethnicity");
    }
    if (has_income_category) {
        income_channels.emplace_back("mean_income_category");
        income_channels.emplace_back("std_income_category");
    }
    if (has_physical_activity) {
        income_channels.emplace_back("mean_physical_activity");
        income_channels.emplace_back("std_physical_activity");
    }

    for (const auto &factor : context.inputs().risk_mapping().entries()) {
        income_channels.emplace_back("mean_" + factor.key().to_string());
        income_channels.emplace_back("std_" + factor.key().to_string());
    }

    for (const auto &disease : context.inputs().diseases()) {
        income_channels.emplace_back("prevalence_" + disease.code.to_string());
        income_channels.emplace_back("incidence_" + disease.code.to_string());
    }

    income_channels.emplace_back("std_yll");
    income_channels.emplace_back("std_yld");
    income_channels.emplace_back("std_daly");

    std::unordered_set<std::string> added_income_channels;
    for (const auto &channel : income_channels) {
        added_income_channels.insert(core::to_lower(channel));
    }

    auto accumulate_squared_diffs_income =
        [&series, &added_income_channels](const std::string &chan, core::Gender sex,
                                          core::Income income, int age, double value) {
            const std::string mean_channel = core::to_lower("mean_" + chan);
            const std::string std_channel = core::to_lower("std_" + chan);

            if (added_income_channels.contains(mean_channel) &&
                added_income_channels.contains(std_channel)) {
                const double mean = series.at(sex, income, mean_channel).at(age);
                const double diff = value - mean;
                series.at(sex, income, std_channel).at(age) += diff * diff;
            }
        };

    const auto current_time = static_cast<unsigned int>(context.time_now());

    for (const auto &person : pop) {
        const unsigned int age = person.age;
        const auto sex = person.gender;
        const auto income = person.income;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                const float expected_life = definition.life_expectancy().at(context.time_now(), sex);
                const double yll = std::max(expected_life - age, 0.0f) * 100'000.0;
                accumulate_squared_diffs_income("yll", sex, income, age, yll);
                accumulate_squared_diffs_income("daly", sex, income, age, yll);
            }
            continue;
        }

        const double dw =
            calculate_disability_weight(definition, residual_disability_weight, person);
        const double yld = dw * 100'000.0;
        accumulate_squared_diffs_income("yld", sex, income, age, yld);
        accumulate_squared_diffs_income("daly", sex, income, age, yld);

        for (const auto &factor : context.inputs().risk_mapping().entries()) {
            const auto key_str = factor.key().to_string();

            if (key_str == "income_category") {
                if (person.income != core::Income::unknown) {
                    accumulate_squared_diffs_income("income_category", sex, income, age,
                                                    person.income_to_value());
                }
                continue;
            }

            if (person.risk_factors.contains(factor.key())) {
                accumulate_squared_diffs_income(key_str, sex, income, age,
                                                person.risk_factors.at(factor.key()));
            }
        }

        if (person.risk_factors.contains("income"_id)) {
            accumulate_squared_diffs_income("income", sex, income, age,
                                            person.risk_factors.at("income"_id));
        }
        if (person.sector != core::Sector::unknown) {
            accumulate_squared_diffs_income("sector", sex, income, age,
                                            person.sector_to_value());
        }
        if (!person.region.empty() && person.region != "unknown") {
            accumulate_squared_diffs_income("region", sex, income, age,
                                            person.region_to_value());
        }
        if (!person.ethnicity.empty() && person.ethnicity != "unknown") {
            accumulate_squared_diffs_income("ethnicity", sex, income, age,
                                            person.ethnicity_to_value());
        }
        if (person.income != core::Income::unknown) {
            accumulate_squared_diffs_income("income_category", sex, income, age,
                                            person.income_to_value());
        }
        if (auto pa_it = person.risk_factors.find("PhysicalActivity"_id);
            pa_it != person.risk_factors.end()) {
            accumulate_squared_diffs_income("physical_activity", sex, income, age, pa_it->second);
        }

    auto divide_by_count_sqrt_income =
        [&series, &added_income_channels](const std::string &chan, core::Gender sex,
                                          core::Income income, int age, double count) {
            const std::string std_channel = core::to_lower("std_" + chan);
            if (!added_income_channels.contains(std_channel)) {
                return;
            }

            if (count > 0) {
                const double sum = series.at(sex, income, std_channel).at(age);
                series.at(sex, income, std_channel).at(age) = std::sqrt(sum / count);
            } else {
                series.at(sex, income, std_channel).at(age) = 0.0;
            }
        };

    const auto age_range = context.inputs().settings().age_range();
    const auto available_income_categories = get_available_income_categories(context);

    for (const auto &income : available_income_categories) {
        for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
            const double count_f = series.at(core::Gender::female, income, "count").at(age);
            const double count_m = series.at(core::Gender::male, income, "count").at(age);
            const double deaths_f = series.at(core::Gender::female, income, "deaths").at(age);
            const double deaths_m = series.at(core::Gender::male, income, "deaths").at(age);

            for (const auto &factor : context.inputs().risk_mapping().entries()) {
                divide_by_count_sqrt_income(factor.key().to_string(), core::Gender::female, income,
                                            age, count_f);
                divide_by_count_sqrt_income(factor.key().to_string(), core::Gender::male, income,
                                            age, count_m);
            }

            for (const auto &channel :
                 {"gender", "income", "sector", "region", "ethnicity",
                  "income_category", "physical_activity"}) {
                divide_by_count_sqrt_income(channel, core::Gender::female, income, age, count_f);
                divide_by_count_sqrt_income(channel, core::Gender::male, income, age, count_m);
            }

            for (const auto &channel : {"yll", "yld", "daly"}) {
                divide_by_count_sqrt_income(channel, core::Gender::female, income, age,
                                            count_f + deaths_f);
                divide_by_count_sqrt_income(channel, core::Gender::male, income, age,
                                            count_m + deaths_m);
            }
        }
    }
}
    }
} // namespace hgps::analysis