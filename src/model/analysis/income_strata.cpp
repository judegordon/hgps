// The income-stratified results: the same statistics, split by income category.
#include "analysis_module.h"

#include "core/income_category_layout.h"
#include "core/string_util.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace hgps::model {
namespace {

const core::Identifier kIncome{"income"};
const core::Identifier kPhysicalActivity{"physicalactivity"};

/// The same list as in series.cpp, and for the same reason: see the comment there.
bool is_demographic_factor(const std::string &lower_key) {
    return lower_key == "gender" || lower_key == "age" || lower_key == "age2" ||
           lower_key == "age3" || lower_key == "region" || lower_key == "ethnicity" ||
           lower_key == "sector" || lower_key == "income_category" || lower_key == "income";
}

} // namespace

void AnalysisModule::calculate_income_based_statistics(RuntimeContext &context,
                                                       ModelResult &result) const {
    // The categories come from the configured layout, in its order — not from whichever
    // categories happen to appear in the cohort. The baseline scans the population for the
    // categories present, so a small cohort silently reports fewer strata than the project has
    // (the same class of problem as its sampled channel list).
    const auto &layout = context.inputs().income_layout();

    ResultByIncome population_by_income{};
    std::map<std::string, ResultByIncomeGender> factor_average;
    std::map<std::string, ResultByIncomeGender> prevalence;
    std::map<unsigned int, ResultByIncomeGender> comorbidity;

    std::map<core::Income, std::map<core::Gender, int>> counts;
    std::map<std::string, std::map<core::Income, std::map<core::Gender, double>>> factor_sums;
    std::map<std::string, std::map<core::Income, std::map<core::Gender, int>>> disease_counts;
    std::map<unsigned int, std::map<core::Income, std::map<core::Gender, int>>> comorbidity_counts;

    for (const auto &person : context.population()) {
        if (!person.is_active() || person.income == core::Income::unknown) {
            continue;
        }

        // A person whose category is not in this project's layout is a bug in income assignment,
        // not something to silently drop — but it is cheap to be sure.
        const auto &strata = layout.strata;
        if (std::find(strata.begin(), strata.end(), person.income) == strata.end()) {
            continue;
        }

        counts[person.income][person.gender] += 1;
        population_by_income.at(person.income) += 1.0;

        for (const auto &entry : context.mapping()) {
            if (entry.level() == 0) {
                continue;
            }
            const auto value = person.risk_factors.find(entry.key());
            if (value == person.risk_factors.end() || std::isnan(value->second)) {
                continue;
            }
            factor_sums[entry.name()][person.income][person.gender] += value->second;
        }

        unsigned int active = 0;
        for (const auto &[disease, state] : person.diseases) {
            if (state.status == DiseaseStatus::active) {
                ++active;
                disease_counts[disease.to_string()][person.income][person.gender] += 1;
            }
        }

        comorbidity_counts[std::min(active, comorbidities_)][person.income][person.gender] += 1;
    }

    const auto share = [&counts](core::Income income, core::Gender gender) {
        const auto by_income = counts.find(income);
        if (by_income == counts.end()) {
            return 1;
        }
        const auto by_gender = by_income->second.find(gender);
        return by_gender == by_income->second.end() ? 1 : std::max(1, by_gender->second);
    };

    for (const auto &[name, by_income] : factor_sums) {
        ResultByIncomeGender value{};
        for (const auto income : layout.strata) {
            const auto found = by_income.find(income);
            if (found == by_income.end()) {
                continue;
            }
            for (const auto gender : {core::Gender::male, core::Gender::female}) {
                const auto sum = found->second.find(gender);
                if (sum != found->second.end()) {
                    value.at(income).at(gender) = sum->second / share(income, gender);
                }
            }
        }
        factor_average.emplace(name, value);
    }

    for (const auto &disease : context.diseases()) {
        ResultByIncomeGender value{};
        const auto found = disease_counts.find(disease.code.to_string());
        if (found != disease_counts.end()) {
            for (const auto income : layout.strata) {
                const auto by_income = found->second.find(income);
                if (by_income == found->second.end()) {
                    continue;
                }
                for (const auto gender : {core::Gender::male, core::Gender::female}) {
                    const auto count = by_income->second.find(gender);
                    if (count != by_income->second.end()) {
                        value.at(income).at(gender) =
                            count->second * 100.0 / share(income, gender);
                    }
                }
            }
        }
        prevalence.emplace(disease.code.to_string(), value);
    }

    for (unsigned int number = 0; number <= comorbidities_; ++number) {
        ResultByIncomeGender value{};
        const auto found = comorbidity_counts.find(number);
        if (found != comorbidity_counts.end()) {
            for (const auto income : layout.strata) {
                const auto by_income = found->second.find(income);
                if (by_income == found->second.end()) {
                    continue;
                }
                for (const auto gender : {core::Gender::male, core::Gender::female}) {
                    const auto count = by_income->second.find(gender);
                    if (count != by_income->second.end()) {
                        value.at(income).at(gender) =
                            count->second * 100.0 / share(income, gender);
                    }
                }
            }
        }
        comorbidity.emplace(number, value);
    }

    result.population_by_income = population_by_income;
    result.risk_factor_average_by_income = std::move(factor_average);
    result.disease_prevalence_by_income = std::move(prevalence);
    result.comorbidity_by_income = std::move(comorbidity);
}

void AnalysisModule::calculate_income_based_series(RuntimeContext &context,
                                                   DataSeries &series) const {
    const auto &layout = context.inputs().income_layout();

    std::set<std::string> available;
    for (const auto &channel : series.channels()) {
        available.insert(core::to_lower(channel));
    }

    const auto accumulate = [&series, &available](core::Gender gender, core::Income income,
                                                  const std::string &channel, std::size_t age,
                                                  double value) {
        if (available.contains(core::to_lower(channel))) {
            series.at(gender, income, channel).at(age) += value;
        }
    };

    for (const auto &person : context.population()) {
        if (!person.is_active() || person.income == core::Income::unknown) {
            continue;
        }

        const auto age = static_cast<std::size_t>(person.age);
        const auto gender = person.gender;
        const auto income = person.income;

        accumulate(gender, income, "count", age, 1.0);

        for (const auto &factor : context.mapping().entries()) {
            const auto key = factor.key().to_string();
            if (is_demographic_factor(key)) {
                continue;
            }
            const auto value = person.risk_factors.find(factor.key());
            if (value != person.risk_factors.end()) {
                accumulate(gender, income, "mean_" + key, age, value->second);
            }
        }

        if (const auto activity = person.risk_factors.find(kPhysicalActivity);
            activity != person.risk_factors.end()) {
            accumulate(gender, income, "mean_physical_activity", age, activity->second);
        }
        if (const auto value = person.risk_factors.find(kIncome);
            value != person.risk_factors.end()) {
            accumulate(gender, income, "mean_income", age, value->second);
        }

        for (const auto &[disease, state] : person.diseases) {
            if (state.status != DiseaseStatus::active) {
                continue;
            }
            accumulate(gender, income, "prevalence_" + disease.to_string(), age, 1.0);
            if (state.start_time == context.time_now()) {
                accumulate(gender, income, "incidence_" + disease.to_string(), age, 1.0);
            }
        }
    }

    // Sums become means, per income category.
    const auto divide = [&series, &available](core::Gender gender, core::Income income,
                                              const std::string &channel, std::size_t age,
                                              double count) {
        if (count > 0.0 && available.contains(core::to_lower(channel))) {
            series.at(gender, income, channel).at(age) /= count;
        }
    };

    const auto age_range = context.age_range();
    for (const auto income : layout.strata) {
        for (int age_value = age_range.lower(); age_value <= age_range.upper(); ++age_value) {
            const auto age = static_cast<std::size_t>(age_value);

            for (const auto gender : {core::Gender::male, core::Gender::female}) {
                const double count = series.at(gender, income, "count").at(age);

                for (const auto &factor : context.mapping().entries()) {
                    const auto key = factor.key().to_string();
                    if (is_demographic_factor(key)) {
                        continue;
                    }
                    divide(gender, income, "mean_" + key, age, count);
                }

                divide(gender, income, "mean_physical_activity", age, count);
                divide(gender, income, "mean_income", age, count);

                for (const auto &disease : context.diseases()) {
                    divide(gender, income, "prevalence_" + disease.code.to_string(), age, count);
                    divide(gender, income, "incidence_" + disease.code.to_string(), age, count);
                }
            }
        }
    }
}

} // namespace hgps::model
