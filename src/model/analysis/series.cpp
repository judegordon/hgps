// The per-age time series: the rows of the result file.
#include "analysis_module.h"

#include "core/string_util.h"
#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kIncome{"income"};
const core::Identifier kPhysicalActivity{"physicalactivity"};

/// Reported from their own members rather than from the factor map, so they must be skipped when
/// the factor map is walked — both when accumulating and when turning sums into means.
///
/// `gender`, `age`, `age2` and `age3` belong here as well, and this is why: the config declares
/// them as level-0 risk factors, so they appear in the mapping, but a person does not carry them
/// in `risk_factors` — they come from `person.gender` and `person.age`. Leaving them out meant
/// `mean_gender` was divided by the head count twice, once here and once in the explicit list, so
/// every male band reported 1/count instead of 1. Found by the equivalence harness against the
/// baseline, which reports 1.
bool is_demographic_factor(const std::string &lower_key) {
    return lower_key == "gender" || lower_key == "age" || lower_key == "age2" ||
           lower_key == "age3" || lower_key == "region" || lower_key == "ethnicity" ||
           lower_key == "sector" || lower_key == "income_category" || lower_key == "income";
}

/// @brief One output channel, resolved to the two vectors it writes.
///
/// The accumulating loops below run per person per year, and several of them ran per *factor* per
/// person per year: each one built `"mean_" + key`, lower-cased it, probed a `std::set<std::string>`
/// and then looked the channel up again by name in a `std::map<std::string, std::vector<double>>`.
/// A whole-run profile of `KevinHall_FINCH` put 868 of 4,238 thread samples — a fifth of the run —
/// in this file for that reason, with `_platform_memcmp` the largest single symbol in the program
/// (docs/performance.md, *After the call-site change*; the previous run's backlog item 9, closed
/// by the change this comment describes).
///
/// Resolving a channel is a map lookup, so it is done once per year per channel rather than once
/// per person per channel. An unresolved channel — one the configuration does not report — keeps
/// null pointers and is skipped, which is what `has()` did for it before.
struct Channel {
    std::vector<double> *male{};
    std::vector<double> *female{};

    std::vector<double> *of(core::Gender gender) const noexcept {
        return gender == core::Gender::male ? male : female;
    }

    void add(core::Gender gender, std::size_t age, double value) const {
        if (auto *values = of(gender); values != nullptr) {
            values->at(age) += value;
        }
    }

    /// @brief Turns the sum into a mean, as the old `divide` did: not for an empty band, and not
    ///        for a channel this configuration does not report.
    void divide(core::Gender gender, std::size_t age, double count) const {
        if (auto *values = of(gender); values != nullptr && count > 0.0) {
            values->at(age) /= count;
        }
    }
};

/// @brief Resolves the channels of the whole-population series, by name, once.
class ChannelResolver {
  public:
    explicit ChannelResolver(DataSeries &series) : series_{&series} {
        for (const auto &channel : series.channels()) {
            available_.insert(core::to_lower(channel));
        }
    }

    Channel operator()(const std::string &name) const {
        if (!available_.contains(core::to_lower(name))) {
            return Channel{};
        }
        return Channel{.male = &series_->at(core::Gender::male, name),
                       .female = &series_->at(core::Gender::female, name)};
    }

  private:
    DataSeries *series_;
    std::set<std::string> available_;
};

/// @brief A risk factor's output channel, and the index its values are stored under.
struct FactorChannel {
    std::uint32_t index{FactorIndex::unknown};
    Channel mean;
};

/// @brief The mapping's non-demographic factors, resolved in mapping order.
///
/// The index comes from `find`, never `intern`: `analyse` runs once a year with the population
/// already built, so a factor some model assigns has been interned by then, and one nothing
/// assigns stays `unknown` — for which `find_index` returns null and the person contributes
/// nothing, exactly as `risk_factors.find(key) == end()` did. Resolving per year rather than once
/// per run is what makes that safe: a name interned later is picked up by the next year's
/// resolution rather than frozen out for the life of the run, which is the defect the previous
/// run's `resolve_predictors` had (docs/SUMMARY.md finding 7).
std::vector<FactorChannel> resolve_factors(const RuntimeContext &context,
                                           const ChannelResolver &resolve) {
    std::vector<FactorChannel> factors;
    factors.reserve(context.mapping().entries().size());
    for (const auto &factor : context.mapping().entries()) {
        const auto &key = factor.key().to_string();
        if (is_demographic_factor(key)) {
            continue;
        }
        factors.push_back(FactorChannel{.index = factor_index().find(factor.key()),
                                        .mean = resolve("mean_" + key)});
    }
    return factors;
}

/// @brief A disease's two channels.
struct DiseaseChannels {
    Channel prevalence;
    Channel incidence;
};

std::map<core::Identifier, DiseaseChannels> resolve_diseases(const RuntimeContext &context,
                                                             const ChannelResolver &resolve) {
    std::map<core::Identifier, DiseaseChannels> diseases;
    for (const auto &disease : context.diseases()) {
        const auto &code = disease.code.to_string();
        diseases.emplace(disease.code,
                         DiseaseChannels{.prevalence = resolve("prevalence_" + code),
                                         .incidence = resolve("incidence_" + code)});
    }
    return diseases;
}

/// @brief The four weight categories, resolved.
struct WeightChannels {
    Channel normal;
    Channel over;
    Channel obese;
    Channel above;
};

/// @brief Counts one person into their weight category.
///
/// A free function rather than a member since this run: what it needs is the classifier and the
/// four resolved channels, and taking them as arguments is what lets the resolution happen once
/// per year in the caller rather than per person here.
void classify_weight(const WeightModel &classifier, const WeightChannels &channels,
                     const Person &person) {
    const auto age = static_cast<std::size_t>(person.age);

    switch (classifier.classify_weight(person)) {
    case WeightCategory::normal:
        channels.normal.add(person.gender, age, 1.0);
        return;
    case WeightCategory::overweight:
        channels.over.add(person.gender, age, 1.0);
        channels.above.add(person.gender, age, 1.0);
        return;
    case WeightCategory::obese:
        channels.obese.add(person.gender, age, 1.0);
        channels.above.add(person.gender, age, 1.0);
        return;
    }

    throw diag::InternalError("unknown weight classification category");
}

} // namespace

void AnalysisModule::calculate_population_statistics(RuntimeContext &context,
                                                     DataSeries &series) const {
    if (!series.channels().empty()) {
        throw diag::InternalError("the result series has already been populated");
    }

    series.add_channels(channels_);

    // Every channel this loop can write, resolved once. See `Channel` above for what this replaced
    // and what it was costing.
    const ChannelResolver resolve{series};

    const auto count_channel = resolve("count");
    const auto deaths = resolve("deaths");
    const auto emigrations = resolve("emigrations");
    const auto mean_gender = resolve("mean_gender");
    const auto mean_region = resolve("mean_region");
    const auto mean_ethnicity = resolve("mean_ethnicity");
    const auto mean_sector = resolve("mean_sector");
    const auto mean_income_category = resolve("mean_income_category");
    const auto mean_income = resolve("mean_income");
    const auto mean_physical_activity = resolve("mean_physical_activity");
    const auto mean_yll = resolve("mean_yll");
    const auto mean_yld = resolve("mean_yld");
    const auto mean_daly = resolve("mean_daly");
    const WeightChannels weight{.normal = resolve("normal_weight"),
                                .over = resolve("over_weight"),
                                .obese = resolve("obese_weight"),
                                .above = resolve("above_weight")};

    const auto factors = resolve_factors(context, resolve);
    const auto diseases = resolve_diseases(context, resolve);

    const auto income_index = factor_index().find(kIncome);
    const auto activity_index = factor_index().find(kPhysicalActivity);

    const auto current_time = static_cast<unsigned int>(context.time_now());

    // Slot order, serially: every row of the output is a sum over the population, and doing it in
    // population order is what makes the file reproducible.
    for (const auto &person : context.population()) {
        const auto age = static_cast<std::size_t>(person.age);
        const auto gender = person.gender;

        if (!person.is_active()) {
            if (!person.is_alive() && person.time_of_death() == current_time) {
                deaths.add(gender, age, 1.0);

                const auto expected = definition_.life_expectancy().at(context.time_now(), gender);
                const double yll =
                    std::max(static_cast<double>(expected) - static_cast<double>(person.age), 0.0) *
                    kDalyUnits;
                mean_yll.add(gender, age, yll);
                mean_daly.add(gender, age, yll);
            }

            if (person.has_emigrated() && person.time_of_migration() == current_time) {
                emigrations.add(gender, age, 1.0);
            }

            continue;
        }

        count_channel.add(gender, age, 1.0);
        mean_gender.add(gender, age, static_cast<double>(person.gender_to_value()));

        if (person.region != "unknown") {
            mean_region.add(gender, age, static_cast<double>(person.region_to_value()));
        }
        if (person.ethnicity != "unknown") {
            mean_ethnicity.add(gender, age, static_cast<double>(person.ethnicity_to_value()));
        }
        if (person.sector != core::Sector::unknown) {
            mean_sector.add(gender, age, static_cast<double>(person.sector_to_value()));
        }
        if (person.income != core::Income::unknown) {
            mean_income_category.add(gender, age, static_cast<double>(person.income_to_value()));
        }
        if (const auto *income = person.risk_factors.find_index(income_index); income != nullptr) {
            mean_income.add(gender, age, *income);
        }
        if (const auto *activity = person.risk_factors.find_index(activity_index);
            activity != nullptr) {
            mean_physical_activity.add(gender, age, *activity);
        }

        // In mapping order, which is the order the loop that built `factors` walked: the sums are
        // per channel, so the order does not reach a value, but keeping it makes the two loops
        // readable as the same loop.
        for (const auto &factor : factors) {
            if (const auto *value = person.risk_factors.find_index(factor.index);
                value != nullptr) {
                factor.mean.add(gender, age, *value);
            }
        }

        for (const auto &[disease, state] : person.diseases) {
            if (state.status != DiseaseStatus::active) {
                continue;
            }
            const auto channels = diseases.find(disease);
            if (channels == diseases.end()) {
                continue;
            }
            channels->second.prevalence.add(gender, age, 1.0);
            if (state.start_time == context.time_now()) {
                channels->second.incidence.add(gender, age, 1.0);
            }
        }

        const double yld = calculate_disability_weight(person) * kDalyUnits;
        mean_yld.add(gender, age, yld);
        mean_daly.add(gender, age, yld);

        classify_weight(classifier_, weight, person);
    }

    // Sums become means, through the same resolved channels the sums were accumulated into.
    const auto age_range = context.age_range();
    for (int age_value = age_range.lower(); age_value <= age_range.upper(); ++age_value) {
        const auto age = static_cast<std::size_t>(age_value);

        const double count_male = series(core::Gender::male, "count").at(age);
        const double count_female = series(core::Gender::female, "count").at(age);
        const double deaths_male = series(core::Gender::male, "deaths").at(age);
        const double deaths_female = series(core::Gender::female, "deaths").at(age);

        for (const auto &factor : factors) {
            factor.mean.divide(core::Gender::male, age, count_male);
            factor.mean.divide(core::Gender::female, age, count_female);
        }

        for (const auto &channel : {mean_gender, mean_region, mean_ethnicity, mean_sector,
                                    mean_income, mean_income_category, mean_physical_activity}) {
            channel.divide(core::Gender::male, age, count_male);
            channel.divide(core::Gender::female, age, count_female);
        }

        for (const auto &[code, channels] : diseases) {
            channels.prevalence.divide(core::Gender::male, age, count_male);
            channels.prevalence.divide(core::Gender::female, age, count_female);
            channels.incidence.divide(core::Gender::male, age, count_male);
            channels.incidence.divide(core::Gender::female, age, count_female);
        }

        // The burden channels are per person-year, so this year's deaths count towards the
        // denominator as well as the living.
        for (const auto &channel : {mean_yll, mean_yld, mean_daly}) {
            channel.divide(core::Gender::male, age, count_male + deaths_male);
            channel.divide(core::Gender::female, age, count_female + deaths_female);
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

    // Resolved once, not once per person per channel. This lambda runs for every person and
    // every channel of every year, and it used to build two strings, lower-case both and look
    // each up in a std::map<std::string> on every call: the lower-casing alone was 6% of the
    // whole run (docs/performance.md). What it needs is the two vectors, so those are found up
    // front and the loop below just adds to them.
    struct Target {
        std::vector<double> *mean{};
        std::vector<double> *deviation{};
    };

    std::map<std::string, std::array<Target, 2>> targets;
    const auto slot_of = [](core::Gender gender) {
        return gender == core::Gender::male ? std::size_t{0} : std::size_t{1};
    };

    for (const auto &channel : series.channels()) {
        const auto lower = core::to_lower(channel);
        if (!lower.starts_with("std_")) {
            continue;
        }

        const auto name = lower.substr(4);
        if (!available.contains("mean_" + name)) {
            continue;
        }

        auto &entry = targets[name];
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            entry.at(slot_of(gender)) = Target{
                .mean = &series(gender, "mean_" + name), .deviation = &series(gender, channel)};
        }
    }

    // The targets, resolved to pointers once. `accumulate` took a name and looked it up in the map
    // above on every call — per person per factor per year, with a `std::string` built from the
    // factor's identifier to ask with. The map is still how they are found; what changed is that
    // they are found once (docs/performance.md, *The analysis module's channels, resolved once a
    // year*).
    using Pair = std::array<Target, 2>;
    const auto find_target = [&targets](const std::string &name) -> const Pair * {
        const auto found = targets.find(name);
        return found == targets.end() ? nullptr : &found->second;
    };

    const auto accumulate = [&slot_of](const Pair *target, core::Gender gender, std::size_t age,
                                       double value) {
        if (target == nullptr) {
            return;
        }
        const auto &side = target->at(slot_of(gender));
        const double difference = value - side.mean->at(age);
        side.deviation->at(age) += difference * difference;
    };

    // The non-demographic factors, in mapping order, with the index their values are stored under.
    // `find` rather than `intern`, per year, for the reason `resolve_factors` gives.
    struct FactorTarget {
        std::uint32_t index{FactorIndex::unknown};
        const Pair *target{};
    };
    std::vector<FactorTarget> factor_targets;
    factor_targets.reserve(context.mapping().entries().size());
    for (const auto &factor : context.mapping().entries()) {
        const auto &key = factor.key().to_string();
        if (is_demographic_factor(key)) {
            continue;
        }
        factor_targets.push_back(FactorTarget{.index = factor_index().find(factor.key()),
                                              .target = find_target(key)});
    }

    const auto *yll_target = find_target("yll");
    const auto *yld_target = find_target("yld");
    const auto *daly_target = find_target("daly");
    const auto *age_target = find_target("age");
    const auto *age2_target = find_target("age2");
    const auto *age3_target = find_target("age3");
    const auto *gender_target = find_target("gender");
    const auto *region_target = find_target("region");
    const auto *ethnicity_target = find_target("ethnicity");
    const auto *sector_target = find_target("sector");
    const auto *income_category_target = find_target("income_category");
    const auto *income_target = find_target("income");
    const auto *activity_target = find_target("physical_activity");

    const auto income_index = factor_index().find(kIncome);
    const auto activity_index = factor_index().find(kPhysicalActivity);

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
                accumulate(yll_target, gender, age, yll);
                accumulate(daly_target, gender, age, yll);
            }
            continue;
        }

        const double yld = calculate_disability_weight(person) * kDalyUnits;
        accumulate(yld_target, gender, age, yld);
        accumulate(daly_target, gender, age, yld);

        const auto age_value = static_cast<double>(person.age);
        accumulate(age_target, gender, age, age_value);
        accumulate(age2_target, gender, age, age_value * age_value);
        accumulate(age3_target, gender, age, age_value * age_value * age_value);
        accumulate(gender_target, gender, age, static_cast<double>(person.gender_to_value()));

        for (const auto &factor : factor_targets) {
            if (const auto *value = person.risk_factors.find_index(factor.index);
                value != nullptr) {
                accumulate(factor.target, gender, age, *value);
            }
        }

        if (person.region != "unknown") {
            accumulate(region_target, gender, age, static_cast<double>(person.region_to_value()));
        }
        if (person.ethnicity != "unknown") {
            accumulate(ethnicity_target, gender, age,
                       static_cast<double>(person.ethnicity_to_value()));
        }
        if (person.sector != core::Sector::unknown) {
            accumulate(sector_target, gender, age, static_cast<double>(person.sector_to_value()));
        }
        if (person.income != core::Income::unknown) {
            accumulate(income_category_target, gender, age,
                       static_cast<double>(person.income_to_value()));
        }
        if (const auto *income = person.risk_factors.find_index(income_index); income != nullptr) {
            accumulate(income_target, gender, age, *income);
        }
        if (const auto *activity = person.risk_factors.find_index(activity_index);
            activity != nullptr) {
            accumulate(activity_target, gender, age, *activity);
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
                                 "income", "income_category", "physical_activity"}) {
            finish(name, core::Gender::male, age, count_male);
            finish(name, core::Gender::female, age, count_female);
        }

        // The burden channels are rates per person-year at risk, so someone who died during the
        // year is in the denominator: they were alive for part of it. Their contribution to the
        // numerator is years of life lost and not years lived with disability, which is why the
        // yld sum comes only from the living and is still divided by the larger figure. All three
        // channels use the same denominator for their mean and their standard deviation — yld
        // used the head count here, which made its spread disagree with the baseline's by about
        // 2% and, worse, disagree with its own mean.
        for (const auto *name : {"yll", "yld", "daly"}) {
            finish(name, core::Gender::male, age, count_male + deaths_male);
            finish(name, core::Gender::female, age, count_female + deaths_female);
        }
    }
}

} // namespace hgps::model
