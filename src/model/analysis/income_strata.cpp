// The income-stratified results: the same statistics, split by income category.
//
// "The same statistics" is the whole point of this file, and until this run it was not true. The
// stratum files carry the same header as the whole-population one, and the result writer writes a
// zero for a channel with no stratified counterpart — which is right for a channel that has none
// and wrong for one that should. On `KevinHall_FINCH` that made **49 columns of every stratum file
// identically zero here and non-zero in the baseline's**: the four weight categories, which the
// previous run fixed, and the 45 this one does (docs/SUMMARY.md).
//
// So this file is now the whole-population series of series.cpp, per stratum, and it is written to
// be read beside it: the same resolve-once discipline, the same two passes, the same denominators.
// Where the two differ, the difference is the baseline's and there is a comment saying so.
#include "analysis_module.h"

#include "core/income_category_layout.h"
#include "core/string_util.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

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

/// @brief A quantity's two channels: the mean the run reports and the spread beside it.
///
/// Either may be null, which is what a configuration that does not report the channel looks like
/// — the same thing `Channel`'s null pointers mean in series.cpp.
struct Pair {
    std::vector<double> *mean{};
    std::vector<double> *deviation{};

    void add(std::size_t age, double value) const {
        if (mean != nullptr) {
            mean->at(age) += value;
        }
    }

    void divide(std::size_t age, double count) const {
        if (mean != nullptr && count > 0.0) {
            mean->at(age) /= count;
        }
    }

    /// @brief Accumulates one person's squared deviation from the **final** mean.
    ///
    /// Called only after `divide`, because that is the order the baseline runs its two passes in
    /// and the mean it subtracts is the finished one.
    void accumulate(std::size_t age, double value) const {
        if (mean == nullptr || deviation == nullptr) {
            return;
        }
        const double difference = value - mean->at(age);
        deviation->at(age) += difference * difference;
    }

    void finish(std::size_t age, double count) const {
        if (deviation == nullptr) {
            return;
        }
        auto &value = deviation->at(age);
        value = count > 0.0 ? std::sqrt(value / count) : 0.0;
    }
};

/// @brief One (income category, sex) stratum's channels, resolved to the vectors they write.
///
/// Resolved on first sighting of the pair rather than for every stratum the layout declares.
/// That is not tidiness: resolving eagerly creates a channel vector per age for strata nobody is
/// in, once per year, and it cost `HLM_France` 5.4 MiB of peak memory the last time this file was
/// touched (docs/performance.md).
struct Stratum {
    // Head counts. Nothing divides them and there is no `std_` column beside any of them — the
    // rule both reductions and the server's summary follow (docs/equivalence-method.md §2).
    std::vector<double> *count{};
    std::vector<double> *deaths{};
    std::vector<double> *emigrations{};
    std::vector<double> *normal_weight{};
    std::vector<double> *over_weight{};
    std::vector<double> *obese_weight{};
    std::vector<double> *above_weight{};

    // Everything with a mean and a spread, by the bare name the columns are `mean_`/`std_` of.
    // The map is what the finishing loops walk; the members below are copies of the same two
    // pointers, so the person loop does no lookup at all.
    std::map<std::string, Pair> pairs;

    Pair gender, region, ethnicity, sector, income_category, income, physical_activity;
    Pair age, age2, age3;
    Pair yll, yld, daly;
    std::vector<Pair> factors;
    std::vector<Pair> prevalence;
    std::vector<Pair> incidence;
};


/// The spread of every stratified quantity, after its mean is final.
///
/// A second pass over the population rather than a running sum of squares, because the deviation
/// is from the *finished* mean and the baseline does it this way — and because the alternative,
/// `E[x²] − E[x]²`, loses most of its significant digits on a quantity whose spread is small
/// beside its level, which several of these are.
///
/// A file-local template rather than a member, because `Stratum` is file-local: the resolved
/// channels cannot appear in a header without putting the whole layout of this file in one. The
/// disability weight comes in as a callable for the same reason, and as a template parameter
/// rather than a `std::function` because it is called once per person per year.
template <class DisabilityWeight>
void income_standard_deviation(RuntimeContext &context, DataSeries &series,
                               std::map<core::Income, std::map<core::Gender, Stratum>> &strata,
                               const AnalysisDefinition &definition,
                               const DisabilityWeight &disability_weight) {
    const auto &layout = context.inputs().income_layout();

    std::set<std::string> available;
    for (const auto &channel : series.channels()) {
        available.insert(core::to_lower(channel));
    }

    const auto income_index = factor_index().find(kIncome);
    const auto activity_index = factor_index().find(kPhysicalActivity);
    const auto current_time = static_cast<unsigned int>(context.time_now());

    std::vector<std::pair<std::uint32_t, std::string>> factors;
    for (const auto &factor : context.mapping().entries()) {
        const auto &key = factor.key().to_string();
        if (is_demographic_factor(key)) {
            continue;
        }
        factors.emplace_back(factor_index().find(factor.key()), key);
    }

    const auto stratum_for = [&strata](core::Income income,
                                       core::Gender gender) -> const Stratum * {
        const auto by_income = strata.find(income);
        if (by_income == strata.end()) {
            return nullptr;
        }
        const auto found = by_income->second.find(gender);
        return found == by_income->second.end() ? nullptr : &found->second;
    };

    for (const auto &person : context.population()) {
        if (person.income == core::Income::unknown) {
            continue;
        }
        const auto *resolved = stratum_for(person.income, person.gender);
        if (resolved == nullptr) {
            continue;
        }
        const auto &stratum = *resolved;
        const auto age = static_cast<std::size_t>(person.age);

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                const auto expected = definition.life_expectancy().at(context.time_now(),
                                                                      person.gender);
                const double yll =
                    std::max(static_cast<double>(expected) - static_cast<double>(person.age), 0.0) *
                    kDalyUnits;
                stratum.yll.accumulate(age, yll);
                stratum.daly.accumulate(age, yll);
            }
            continue;
        }

        const double yld = disability_weight(person) * kDalyUnits;
        stratum.yld.accumulate(age, yld);
        stratum.daly.accumulate(age, yld);

        const auto age_value = static_cast<double>(person.age);
        stratum.age.accumulate(age, age_value);
        stratum.age2.accumulate(age, age_value * age_value);
        stratum.age3.accumulate(age, age_value * age_value * age_value);
        stratum.gender.accumulate(age, static_cast<double>(person.gender_to_value()));

        for (std::size_t position = 0; position < factors.size(); ++position) {
            if (const auto *value = person.risk_factors.find_index(factors[position].first);
                value != nullptr) {
                stratum.factors[position].accumulate(age, *value);
            }
        }

        if (person.region != "unknown") {
            stratum.region.accumulate(age, static_cast<double>(person.region_to_value()));
        }
        if (person.ethnicity != "unknown") {
            stratum.ethnicity.accumulate(age, static_cast<double>(person.ethnicity_to_value()));
        }
        if (person.sector != core::Sector::unknown) {
            stratum.sector.accumulate(age, static_cast<double>(person.sector_to_value()));
        }
        stratum.income_category.accumulate(age,
                                           static_cast<double>(person.income_to_value()));
        if (const auto *value = person.risk_factors.find_index(income_index); value != nullptr) {
            stratum.income.accumulate(age, *value);
        }
        if (const auto *activity = person.risk_factors.find_index(activity_index);
            activity != nullptr) {
            stratum.physical_activity.accumulate(age, *activity);
        }
    }

    // --- sums of squares become standard deviations --------------------------------------------
    //
    // The order, and the repetition, are the baseline's and are reproduced deliberately. Its
    // finishing loop walks **every mapping entry** and then a fixed list of demographic names, and
    // a name in both — `KevinHall_FINCH` declares `Region`, `Ethnicity`, `Income`,
    // `income_category`, `Age`, `Age2`, `Age3` and `Gender` as level-0 risk factors — has the
    // square root taken **twice**. So `std_region` there is `sqrt(sqrt(Σd²/n)/n)` rather than
    // `sqrt(Σd²/n)`: 0.122097 where the spread of a 1-to-4 category with mean 1.38 over 50 people
    // is 0.745.
    //
    // That is an upstream defect (docs/upstream-reports.md), and this build reproduces it here for
    // the same reason series.cpp reproduces it in the whole-population series: the two files agree
    // column for column, which is what the equivalence harness is measuring. Fixing it is a
    // deviation with a compatibility flag, not a quiet correction, and it belongs to whoever owns
    // the model.
    const auto finish = [&strata](core::Income income, core::Gender gender,
                                  const std::string &name, std::size_t age, double count) {
        const auto by_income = strata.find(income);
        if (by_income == strata.end()) {
            return;
        }
        const auto found = by_income->second.find(gender);
        if (found == by_income->second.end()) {
            return;
        }
        const auto pair = found->second.pairs.find(name);
        if (pair == found->second.pairs.end()) {
            return;
        }
        pair->second.finish(age, count);
    };

    const auto age_range = context.age_range();
    for (const auto income : layout.strata) {
        for (int age_value = age_range.lower(); age_value <= age_range.upper(); ++age_value) {
            const auto age = static_cast<std::size_t>(age_value);

            for (const auto gender : {core::Gender::male, core::Gender::female}) {
                const double count = series.at(gender, income, "count").at(age);
                const double deaths = available.contains("deaths")
                                          ? series.at(gender, income, "deaths").at(age)
                                          : 0.0;

                for (const auto &factor : context.mapping().entries()) {
                    finish(income, gender, factor.key().to_string(), age, count);
                }

                for (const auto *name : {"age", "age2", "age3", "gender", "region", "ethnicity",
                                         "sector", "income", "income_category",
                                         "physical_activity"}) {
                    finish(income, gender, name, age, count);
                }

                for (const auto *name : {"yll", "yld", "daly"}) {
                    finish(income, gender, name, age, count + deaths);
                }
            }
        }
    }
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

    // A channel resolved for one (stratum, sex), or nulls when this configuration does not report
    // it. `series.at(gender, income, key)` creates the vector, which is why this is only ever
    // called from `stratum_for` — that is, only for a (stratum, sex) somebody is actually in.
    const auto resolve = [&series, &available](core::Gender gender, core::Income income,
                                               const std::string &channel)
        -> std::vector<double> * {
        return available.contains(core::to_lower(channel)) ? &series.at(gender, income, channel)
                                                           : nullptr;
    };

    // The non-demographic factors, in mapping order, with the index their values are stored under.
    // `find` rather than `intern`, and per year rather than per run, for the reason series.cpp's
    // `resolve_factors` gives.
    struct Factor {
        std::uint32_t index{FactorIndex::unknown};
        std::string key;
    };
    std::vector<Factor> factors;
    for (const auto &factor : context.mapping().entries()) {
        const auto &key = factor.key().to_string();
        if (is_demographic_factor(key)) {
            continue;
        }
        factors.push_back(Factor{.index = factor_index().find(factor.key()), .key = key});
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
        stratum.deaths = resolve(gender, income, "deaths");
        stratum.emigrations = resolve(gender, income, "emigrations");
        stratum.normal_weight = resolve(gender, income, "normal_weight");
        stratum.over_weight = resolve(gender, income, "over_weight");
        stratum.obese_weight = resolve(gender, income, "obese_weight");
        stratum.above_weight = resolve(gender, income, "above_weight");

        // A name's two channels, remembered under the bare name so the finishing loops below can
        // walk the mapping and the demographic list the way the baseline does.
        const auto pair = [&](const std::string &name) {
            const Pair resolved{.mean = resolve(gender, income, "mean_" + name),
                                .deviation = resolve(gender, income, "std_" + name)};
            stratum.pairs.emplace(name, resolved);
            return resolved;
        };

        stratum.age = pair("age");
        stratum.age2 = pair("age2");
        stratum.age3 = pair("age3");
        stratum.gender = pair("gender");
        stratum.region = pair("region");
        stratum.ethnicity = pair("ethnicity");
        stratum.sector = pair("sector");
        stratum.income_category = pair("income_category");
        stratum.income = pair("income");
        stratum.physical_activity = pair("physical_activity");
        stratum.yll = pair("yll");
        stratum.yld = pair("yld");
        stratum.daly = pair("daly");

        for (const auto &factor : factors) {
            stratum.factors.push_back(pair(factor.key));
        }

        // Prevalence and incidence have no `std_` column in any configuration, so only the mean
        // half of the pair is ever set — kept as a `Pair` so one `divide` serves everything.
        for (const auto &disease : context.diseases()) {
            const auto &code = disease.code.to_string();
            stratum.prevalence.push_back(
                Pair{.mean = resolve(gender, income, "prevalence_" + code)});
            stratum.incidence.push_back(
                Pair{.mean = resolve(gender, income, "incidence_" + code)});
        }

        return &by_gender.emplace(gender, std::move(stratum)).first->second;
    };

    // Which disease is which column, for the person loop: a person carries only the diseases they
    // have, so the two lists cannot be walked in step.
    std::map<core::Identifier, std::size_t> disease_position;
    for (std::size_t position = 0; position < context.diseases().size(); ++position) {
        disease_position.emplace(context.diseases()[position].code, position);
    }

    const auto current_time = static_cast<unsigned int>(context.time_now());

    // --- pass one: the sums -------------------------------------------------------------------
    for (const auto &person : context.population()) {
        if (person.income == core::Income::unknown) {
            continue;
        }
        const auto *resolved = stratum_for(person.income, person.gender);
        if (resolved == nullptr) {
            continue;
        }
        const auto &stratum = *resolved;
        const auto age = static_cast<std::size_t>(person.age);

        // Somebody who died or emigrated this year, counted in the stratum they were in. The
        // whole-population series does exactly this in series.cpp; this file skipped every
        // inactive person outright, which is why `deaths`, `emigrations`, `mean_yll` and
        // `mean_daly` were four of the 45 empty columns.
        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                if (stratum.deaths != nullptr) {
                    stratum.deaths->at(age) += 1.0;
                }

                const auto expected = definition_.life_expectancy().at(context.time_now(),
                                                                       person.gender);
                const double yll =
                    std::max(static_cast<double>(expected) - static_cast<double>(person.age), 0.0) *
                    kDalyUnits;
                stratum.yll.add(age, yll);
                stratum.daly.add(age, yll);
            }

            if (person.has_emigrated() && person.time_of_migration() == current_time &&
                stratum.emigrations != nullptr) {
                stratum.emigrations->at(age) += 1.0;
            }

            continue;
        }

        if (stratum.count != nullptr) {
            stratum.count->at(age) += 1.0;
        }

        stratum.gender.add(age, static_cast<double>(person.gender_to_value()));

        if (person.region != "unknown") {
            stratum.region.add(age, static_cast<double>(person.region_to_value()));
        }
        if (person.ethnicity != "unknown") {
            stratum.ethnicity.add(age, static_cast<double>(person.ethnicity_to_value()));
        }
        if (person.sector != core::Sector::unknown) {
            stratum.sector.add(age, static_cast<double>(person.sector_to_value()));
        }
        // Unconditional, unlike the four above: this loop has already established that the
        // category is known — it is the stratum.
        stratum.income_category.add(age, static_cast<double>(person.income_to_value()));

        if (const auto *value = person.risk_factors.find_index(income_index); value != nullptr) {
            stratum.income.add(age, *value);
        }
        if (const auto *activity = person.risk_factors.find_index(activity_index);
            activity != nullptr) {
            stratum.physical_activity.add(age, *activity);
        }

        for (std::size_t position = 0; position < factors.size(); ++position) {
            if (const auto *value = person.risk_factors.find_index(factors[position].index);
                value != nullptr) {
                stratum.factors[position].add(age, *value);
            }
        }

        for (const auto &[disease, state] : person.diseases) {
            if (state.status != DiseaseStatus::active) {
                continue;
            }
            const auto position = disease_position.find(disease);
            if (position == disease_position.end()) {
                continue;
            }
            stratum.prevalence[position->second].add(age, 1.0);
            if (state.start_time == context.time_now()) {
                stratum.incidence[position->second].add(age, 1.0);
            }
        }

        const double yld = calculate_disability_weight(person) * kDalyUnits;
        stratum.yld.add(age, yld);
        stratum.daly.add(age, yld);

        // The weight categories are head counts, not means, so nothing divides them below — the
        // same rule the whole-population series follows in series.cpp, and the same rule the
        // harness and the server reduce them by.
        switch (classifier_.classify_weight(person)) {
        case WeightCategory::normal:
            if (stratum.normal_weight != nullptr) {
                stratum.normal_weight->at(age) += 1.0;
            }
            break;
        case WeightCategory::overweight:
            if (stratum.over_weight != nullptr) {
                stratum.over_weight->at(age) += 1.0;
            }
            if (stratum.above_weight != nullptr) {
                stratum.above_weight->at(age) += 1.0;
            }
            break;
        case WeightCategory::obese:
            if (stratum.obese_weight != nullptr) {
                stratum.obese_weight->at(age) += 1.0;
            }
            if (stratum.above_weight != nullptr) {
                stratum.above_weight->at(age) += 1.0;
            }
            break;
        }
    }

    // --- sums become means --------------------------------------------------------------------
    const auto age_range = context.age_range();
    for (const auto income : layout.strata) {
        for (int age_value = age_range.lower(); age_value <= age_range.upper(); ++age_value) {
            const auto age = static_cast<std::size_t>(age_value);
            const auto age_double = static_cast<double>(age_value);

            for (const auto gender : {core::Gender::male, core::Gender::female}) {
                // Asked for by name, for every stratum the layout declares and whether or not
                // anybody is in it, because that is what creates the channel:
                // `has_income_channels()` — and so whether the stratum files carry rows at all —
                // depends on it.
                const double count = series.at(gender, income, "count").at(age);
                const double deaths = available.contains("deaths")
                                          ? series.at(gender, income, "deaths").at(age)
                                          : 0.0;

                // Age is the row's own key, so its mean is exact rather than accumulated — and it
                // is written for **every** configured stratum rather than only the inhabited
                // ones, because that is what the baseline does. It is the whole difference on the
                // two HLM examples, where nobody has an income category at all and these three
                // columns are the only non-zero ones in any stratum file
                // (docs/equivalence.md, the coverage table).
                if (available.contains("mean_age")) {
                    series.at(gender, income, "mean_age").at(age) = age_double;
                }
                if (available.contains("mean_age2")) {
                    series.at(gender, income, "mean_age2").at(age) = age_double * age_double;
                }
                if (available.contains("mean_age3")) {
                    series.at(gender, income, "mean_age3").at(age) =
                        age_double * age_double * age_double;
                }

                const auto by_income = strata.find(income);
                if (by_income == strata.end()) {
                    continue;
                }
                const auto found = by_income->second.find(gender);
                if (found == by_income->second.end()) {
                    continue;
                }
                const auto &stratum = found->second;

                for (const auto &pair : stratum.factors) {
                    pair.divide(age, count);
                }
                for (const auto &pair : {stratum.gender, stratum.region, stratum.ethnicity,
                                         stratum.sector, stratum.income_category, stratum.income,
                                         stratum.physical_activity}) {
                    pair.divide(age, count);
                }
                for (std::size_t position = 0; position < stratum.prevalence.size(); ++position) {
                    stratum.prevalence[position].divide(age, count);
                    stratum.incidence[position].divide(age, count);
                }

                // The burden channels are per person-year, so this year's deaths count towards
                // the denominator as well as the living — the same denominator series.cpp uses.
                for (const auto &pair : {stratum.yll, stratum.yld, stratum.daly}) {
                    pair.divide(age, count + deaths);
                }
            }
        }
    }

    income_standard_deviation(context, series, strata, definition_,
                              [this](const Person &person) {
                                  return calculate_disability_weight(person);
                              });
}


} // namespace hgps::model
