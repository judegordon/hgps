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

    // One stratum's channels, resolved to the vectors they write.
    //
    // This loop is the one the previous run's backlog item 9 named: it built `"mean_" + key` per
    // factor per person per year, lower-cased it, probed a `std::set<std::string>` and then looked
    // the channel up again by name in a `std::map<std::string, ...>` two levels down. The same
    // treatment as the whole-population series in series.cpp — resolve once, accumulate through
    // the pointer.
    //
    // Resolved on first sighting of a (stratum, sex) rather than for every stratum the layout
    // declares. That is not tidiness: resolving eagerly creates a channel vector per age for
    // strata nobody is in, once per year, and it cost `HLM_France` 5.4 MiB of peak memory —
    // 42.8 to 48.2 — which is a real regression for a change whose whole point is that it costs
    // nothing. Created on demand, the set of vectors that exist is the set that existed before.
    struct Stratum {
        std::vector<double> *count{};
        std::vector<std::vector<double> *> factor_means;
        std::vector<double> *physical_activity{};
        std::vector<double> *income{};
        std::vector<std::vector<double> *> prevalence;
        std::vector<std::vector<double> *> incidence;
        std::vector<double> *normal_weight{};
        std::vector<double> *over_weight{};
        std::vector<double> *obese_weight{};
        std::vector<double> *above_weight{};
    };

    const auto resolve = [&series, &available](core::Gender gender, core::Income income,
                                               const std::string &channel) {
        return available.contains(core::to_lower(channel))
                   ? &series.at(gender, income, channel)
                   : nullptr;
    };

    const auto add = [](std::vector<double> *values, std::size_t age, double value) {
        if (values != nullptr) {
            values->at(age) += value;
        }
    };

    // The non-demographic factors, in mapping order, with the index their values are stored under.
    // `find` rather than `intern`, and per year rather than per run, for the reason series.cpp's
    // `resolve_factors` gives.
    struct Factor {
        std::uint32_t index{FactorIndex::unknown};
        std::string channel;
    };
    std::vector<Factor> factors;
    for (const auto &factor : context.mapping().entries()) {
        const auto &key = factor.key().to_string();
        if (is_demographic_factor(key)) {
            continue;
        }
        factors.push_back(Factor{.index = factor_index().find(factor.key()),
                                 .channel = "mean_" + key});
    }

    const auto income_index = factor_index().find(kIncome);
    const auto activity_index = factor_index().find(kPhysicalActivity);

    const std::set<core::Income> declared{layout.strata.begin(), layout.strata.end()};
    std::map<core::Income, std::map<core::Gender, Stratum>> strata;

    const auto stratum_for = [&](core::Income income, core::Gender gender) -> const Stratum * {
        // A category outside the project's layout contributes nothing, which is what happened
        // before as well: the accumulation created a stratum for it and no file is ever written
        // for a stratum the layout does not name. `calculate_income_based_statistics` above skips
        // the same people for the same reason, and says so.
        if (!declared.contains(income)) {
            return nullptr;
        }

        auto &by_gender = strata[income];
        const auto found = by_gender.find(gender);
        if (found != by_gender.end()) {
            return &found->second;
        }

        Stratum stratum;
        stratum.count = resolve(gender, income, "count");
        for (const auto &factor : factors) {
            stratum.factor_means.push_back(resolve(gender, income, factor.channel));
        }
        stratum.physical_activity = resolve(gender, income, "mean_physical_activity");
        stratum.income = resolve(gender, income, "mean_income");
        for (const auto &disease : context.diseases()) {
            const auto &code = disease.code.to_string();
            stratum.prevalence.push_back(resolve(gender, income, "prevalence_" + code));
            stratum.incidence.push_back(resolve(gender, income, "incidence_" + code));
        }
        stratum.normal_weight = resolve(gender, income, "normal_weight");
        stratum.over_weight = resolve(gender, income, "over_weight");
        stratum.obese_weight = resolve(gender, income, "obese_weight");
        stratum.above_weight = resolve(gender, income, "above_weight");
        return &by_gender.emplace(gender, std::move(stratum)).first->second;
    };

    // Which disease is which column, for the person loop: a person carries only the diseases they
    // have, so the two lists cannot be walked in step.
    std::map<core::Identifier, std::size_t> disease_position;
    for (std::size_t position = 0; position < context.diseases().size(); ++position) {
        disease_position.emplace(context.diseases()[position].code, position);
    }

    for (const auto &person : context.population()) {
        if (!person.is_active() || person.income == core::Income::unknown) {
            continue;
        }

        const auto *resolved = stratum_for(person.income, person.gender);
        if (resolved == nullptr) {
            continue;
        }
        const auto &stratum = *resolved;

        const auto age = static_cast<std::size_t>(person.age);

        add(stratum.count, age, 1.0);

        for (std::size_t position = 0; position < factors.size(); ++position) {
            if (const auto *value = person.risk_factors.find_index(factors[position].index);
                value != nullptr) {
                add(stratum.factor_means[position], age, *value);
            }
        }

        if (const auto *activity = person.risk_factors.find_index(activity_index);
            activity != nullptr) {
            add(stratum.physical_activity, age, *activity);
        }
        if (const auto *value = person.risk_factors.find_index(income_index); value != nullptr) {
            add(stratum.income, age, *value);
        }

        for (const auto &[disease, state] : person.diseases) {
            if (state.status != DiseaseStatus::active) {
                continue;
            }
            const auto position = disease_position.find(disease);
            if (position == disease_position.end()) {
                continue;
            }
            add(stratum.prevalence[position->second], age, 1.0);
            if (state.start_time == context.time_now()) {
                add(stratum.incidence[position->second], age, 1.0);
            }
        }

        // The weight categories, which this series left empty until this run: the four columns
        // existed in every stratum file and every value in them was zero, while the baseline fills
        // them (`analysis_module.cpp:1352-1367`). They are head counts, not means, so nothing
        // divides them below — the same rule the whole-population series follows in series.cpp,
        // and the same rule the harness and the server now reduce them by.
        //
        // They are the four columns of the 49 this file still leaves empty that this run fixed;
        // docs/backlog.md item 2 has the other 45 and the measurement behind them.
        switch (classifier_.classify_weight(person)) {
        case WeightCategory::normal:
            add(stratum.normal_weight, age, 1.0);
            break;
        case WeightCategory::overweight:
            add(stratum.over_weight, age, 1.0);
            add(stratum.above_weight, age, 1.0);
            break;
        case WeightCategory::obese:
            add(stratum.obese_weight, age, 1.0);
            add(stratum.above_weight, age, 1.0);
            break;
        }
    }

    // Sums become means, per income category — through the same resolved channels the sums went
    // into. The weight categories are head counts and are not here, in either reduction that reads
    // them (docs/equivalence-method.md §2).
    const auto divide = [](std::vector<double> *values, std::size_t age, double count) {
        if (values != nullptr && count > 0.0) {
            values->at(age) /= count;
        }
    };

    const auto age_range = context.age_range();
    for (const auto income : layout.strata) {
        for (int age_value = age_range.lower(); age_value <= age_range.upper(); ++age_value) {
            const auto age = static_cast<std::size_t>(age_value);

            for (const auto gender : {core::Gender::male, core::Gender::female}) {
                // `count` is asked for by name here, for every stratum the layout declares and
                // whether or not anybody is in it, because that is what the old loop did and it is
                // what creates the channel: `has_income_channels()` — and so whether the stratum
                // files carry rows at all — depends on it.
                const double count = series.at(gender, income, "count").at(age);

                const auto by_income = strata.find(income);
                if (by_income == strata.end()) {
                    continue;
                }
                const auto found = by_income->second.find(gender);
                if (found == by_income->second.end()) {
                    continue;
                }
                const auto &stratum = found->second;

                for (auto *values : stratum.factor_means) {
                    divide(values, age, count);
                }

                divide(stratum.physical_activity, age, count);
                divide(stratum.income, age, count);

                for (std::size_t position = 0; position < stratum.prevalence.size(); ++position) {
                    divide(stratum.prevalence[position], age, count);
                    divide(stratum.incidence[position], age, count);
                }
            }
        }
    }
}

} // namespace hgps::model
