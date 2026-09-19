// The income-stratified series, driven directly over a small cohort.
//
// The first test this project had of `calculate_income_based_series` was written by the previous
// run, for four columns of the 49 that were identically zero here and non-zero in the baseline's
// stratum files. This run fills the other 45 and this file pins them
// (docs/SUMMARY.md).
//
// **What the columns are checked against.** Two things, and they are complementary:
//
//   * here, hand-computed values over a cohort small enough to add up in the head — which is the
//     only way to say "this column means what the baseline's definition says it means" rather
//     than "this column agrees with a number we also produced";
//   * and the equivalence harness, which since this run reduces and compares **every** CSV a run
//     writes against the baseline's own output, 20 seeds of `KevinHall_FINCH` — which is where
//     the 45 are checked at the scale and in the combinations a fixture cannot reach.
//
// The 33 `std_` columns are not 33 separate assertions, and saying so is the point: they are four
// mechanisms — a mapping factor, a demographic read from the person, the dedicated
// physical-activity channel, and the three burden channels with their own denominator — and each
// mechanism is pinned to a number here. The per-column check is the harness's.
#include "model/analysis/analysis_module.h"

#include "model/runtime_context.h"
#include "sim/scenario.h"

#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::core::Gender;
using hgps::core::Income;
using hgps::core::Sector;
using hgps::model::AnalysisDefinition;
using hgps::model::AnalysisModule;
using hgps::model::DiseaseStatus;
using hgps::model::HierarchicalMapping;
using hgps::model::LmsDefinition;
using hgps::model::MappingEntry;
using hgps::model::RuntimeContext;
using hgps::model::WeightModel;

constexpr int kAgeUpper = 80;
constexpr int kYear = 2020;
constexpr unsigned int kAge = 40;
constexpr double kDaly = 100'000.0;

/// Every person is this old, so one age band carries the whole cohort and every expected value is
/// a sum over three or two people rather than over a distribution.
constexpr std::size_t kBand = kAge;

/// The one disease, and the weight a person who has it carries. `observed_yld` is a table of zeros,
/// so the residual disability weight is zero and a person's weight is exactly this or nothing —
/// which is what makes `mean_yld` predictable.
constexpr float kAsthmaWeight = 0.2F;

/// What `calculate_disability_weight` returns for a person with asthma and nothing else.
///
/// Written as the same two operations the module performs — the complement of the complement, in
/// double, of the `float` the definition stores — rather than as `0.2`. A float stored and
/// complemented twice is not 0.2 to the last bit, and a test that asserted it was would be
/// asserting something about `double` rather than about this column.
const double kAsthmaBurden = 1.0 - (1.0 - static_cast<double>(kAsthmaWeight));

/// The growth reference only has to cover the child cut-off age for the classifier to be
/// constructible; every person here is an adult, so all of them are classified by the BMI
/// cut-offs of 25 and 30 rather than by a z-score.
LmsDefinition child_reference() {
    hgps::model::LmsDataset dataset;
    for (unsigned int age = 0; age <= 18; ++age) {
        dataset[age][Gender::male] = hgps::model::LmsRecord{.lambda = -1.0, .mu = 16.0,
                                                            .sigma = 0.1};
        dataset[age][Gender::female] = hgps::model::LmsRecord{.lambda = -1.0, .mu = 16.0,
                                                              .sigma = 0.1};
    }
    return LmsDefinition{std::move(dataset)};
}

/// Life expectancy is 80 in every year for both sexes, so a person who dies at 40 loses exactly
/// 40 years and `mean_yll` is arithmetic rather than a lookup.
AnalysisDefinition analysis_definition() {
    const hgps::core::IntegerInterval age_range{0, kAgeUpper};
    std::vector<int> years{kYear, kYear + 1};
    const std::vector<Gender> columns{Gender::male, Gender::female};
    hgps::core::Array2D<float> life_expectancy{years.size(), columns.size(), 80.0F};

    return AnalysisDefinition{
        hgps::model::GenderTable<int, float>{hgps::model::MonotonicVector<int>{years}, columns,
                                             std::move(life_expectancy)},
        hgps::model::create_age_gender_table<double>(age_range),
        std::map<hgps::core::Identifier, float>{
            {hgps::core::Identifier{"asthma"}, kAsthmaWeight}}};
}

/// `Region` is deliberately **not** here. It is in the mapping of the one test that needs it, and
/// what that test is about is what happens when a demographic name is also a declared risk factor
/// — which is the baseline's double square root. Everywhere else the mapping is kept clear of the
/// demographic names so each standard deviation is the plain one and can be computed by hand.
HierarchicalMapping mapping(bool region_is_a_declared_factor = false) {
    std::vector<MappingEntry> entries;
    entries.emplace_back("Gender", 0);
    entries.emplace_back("Age", 0);
    if (region_is_a_declared_factor) {
        entries.emplace_back("Region", 0);
    }
    entries.emplace_back("BMI", 2);
    return HierarchicalMapping{std::move(entries)};
}

/// One person, as the test describes them.
struct Spec {
    Income income{};
    Gender gender{};
    double bmi{};
    std::string region;
    std::string ethnicity;
    Sector sector{};
    double income_value{};
    double physical_activity{};

    /// `active`: alive and present. `dead` and `emigrated` left this year.
    enum class State { active, dead, emigrated } state{State::active};

    /// The year asthma started, or nullopt for a person who does not have it.
    std::optional<int> asthma_since;
};

struct Cohort {
    std::shared_ptr<const hgps::model::ModelInput> inputs;
    std::unique_ptr<RuntimeContext> context;
};

Cohort make_cohort(const std::vector<Spec> &people, bool region_is_a_declared_factor = false) {
    hgps::config::ProjectRequirements requirements;
    requirements.income.enabled = true;
    requirements.income.categories = "4";
    requirements.income.income_based_csv_output = true;
    requirements.demographics.region = true;
    requirements.demographics.ethnicity = true;
    requirements.physical_activity.enabled = true;

    const hgps::model::Settings settings{
        .country = hgps::core::Country{.code = 826,
                                       .name = "United Kingdom",
                                       .alpha2 = "GB",
                                       .alpha3 = "GBR"},
        .size_fraction = 1.0,
        .age_range = hgps::core::IntegerInterval{0, kAgeUpper}};

    const hgps::model::RunInfo run{.start_time = kYear,
                                   .stop_time = kYear + 1,
                                   .seed = 42};

    Cohort cohort;
    cohort.inputs = std::make_shared<const hgps::model::ModelInput>(
        hgps::core::DataTable{}, settings, run,
        hgps::model::SesDefinition{.function_name = "normal", .parameters = {0.0, 1.0}},
        mapping(region_is_a_declared_factor),
        std::vector<hgps::core::DiseaseInfo>{
            hgps::core::DiseaseInfo{.code = hgps::core::Identifier{"asthma"}, .name = "Asthma"}},
        requirements);

    cohort.context = std::make_unique<RuntimeContext>(
        cohort.inputs, std::make_unique<hgps::sim::BaselineScenario>(), 42U);
    cohort.context->start_run(1, 42U, people.size());
    cohort.context->set_current_time(kYear);

    std::size_t index = 0;
    for (const auto &spec : people) {
        auto &person = cohort.context->population()[index];
        person.gender = spec.gender;
        person.age = kAge;
        person.income = spec.income;
        person.region = spec.region;
        person.ethnicity = spec.ethnicity;
        person.sector = spec.sector;
        person.risk_factors[hgps::core::Identifier{"bmi"}] = spec.bmi;
        person.risk_factors[hgps::core::Identifier{"income"}] = spec.income_value;
        person.risk_factors[hgps::core::Identifier{"physicalactivity"}] = spec.physical_activity;

        if (spec.asthma_since.has_value()) {
            person.diseases[hgps::core::Identifier{"asthma"}] =
                hgps::model::Disease{.status = DiseaseStatus::active,
                                     .start_time = *spec.asthma_since};
        }

        switch (spec.state) {
        case Spec::State::active:
            break;
        case Spec::State::dead:
            person.die(kYear);
            break;
        case Spec::State::emigrated:
            person.emigrate(kYear);
            break;
        }

        ++index;
    }

    return cohort;
}

/// Three in the low stratum plus one who died there, two in the high stratum plus one who left it,
/// and nobody at all in the two middle strata.
///
/// The empty strata are not padding: the baseline writes `mean_age`, `mean_age2` and `mean_age3`
/// into **every** configured stratum whether or not anybody is in it, and on the two HLM examples
/// — where nobody has an income category at all — those three are the only non-zero columns any
/// stratum file has. They were three of the 45.
const std::vector<Spec> &cohort_specs() {
    static const std::vector<Spec> people{
        {.income = Income::low, .gender = Gender::male, .bmi = 21.0, .region = "region1",
         .ethnicity = "ethnicity2", .sector = Sector::urban, .income_value = 100.0,
         .physical_activity = 1.6},
        {.income = Income::low, .gender = Gender::male, .bmi = 27.0, .region = "region3",
         .ethnicity = "ethnicity2", .sector = Sector::rural, .income_value = 200.0,
         .physical_activity = 1.8, .asthma_since = kYear},
        {.income = Income::low, .gender = Gender::male, .bmi = 33.0, .region = "region2",
         .ethnicity = "ethnicity4", .sector = Sector::urban, .income_value = 300.0,
         .physical_activity = 2.0, .asthma_since = kYear - 1},
        {.income = Income::low, .gender = Gender::male, .bmi = 25.0, .region = "region1",
         .ethnicity = "ethnicity1", .sector = Sector::urban, .income_value = 150.0,
         .physical_activity = 1.7, .state = Spec::State::dead},
        {.income = Income::high, .gender = Gender::female, .bmi = 22.0, .region = "region1",
         .ethnicity = "ethnicity1", .sector = Sector::urban, .income_value = 400.0,
         .physical_activity = 1.5},
        {.income = Income::high, .gender = Gender::female, .bmi = 31.0, .region = "region4",
         .ethnicity = "ethnicity3", .sector = Sector::rural, .income_value = 500.0,
         .physical_activity = 1.9, .asthma_since = kYear - 2},
        {.income = Income::high, .gender = Gender::female, .bmi = 28.0, .region = "region2",
         .ethnicity = "ethnicity2", .sector = Sector::rural, .income_value = 450.0,
         .physical_activity = 1.4, .state = Spec::State::emigrated},
    };
    return people;
}

/// The value of a stratified channel at one age, or zero when that (stratum, sex) never touched
/// it. The stratified channels are created on first use, so a stratum nobody is in has no vector
/// at all — and the result writer reads them exactly this way, through `DataSeries::find`, writing
/// a zero for a channel that is not there.
double stratified(const hgps::model::DataSeries &series, Gender gender, Income income,
                  const std::string &channel, std::size_t age = kBand) {
    const auto *values = series.find(gender, income, channel);
    return values == nullptr ? 0.0 : values->at(age);
}

/// The module is neither copyable nor movable — it is a simulation module — so it is built as a
/// prvalue here and configured through a reference below.
AnalysisModule make_module() {
    return AnalysisModule{analysis_definition(), WeightModel{child_reference()},
                          hgps::core::IntegerInterval{0, kAgeUpper}, 5U};
}

/// Income analysis on, and every dimension this cohort gives a person declared as assigned — which
/// is what puts `mean_region`, `mean_ethnicity`, `mean_sector`, `mean_income_category`,
/// `mean_income` and `mean_physical_activity` in the channel list at all (src/model/analysis/channels.cpp).
void configure(AnalysisModule &module, bool income_analysis = true) {
    module.set_income_analysis_enabled(income_analysis);
    module.set_assigned_attributes(hgps::model::AssignedAttributes{.income_category = true,
                                                                   .income = true,
                                                                   .physical_activity = true,
                                                                   .region = true,
                                                                   .ethnicity = true,
                                                                   .sector = true});
}

/// Runs one year of analysis over the standard cohort and returns its series.
hgps::model::ModelResult analyse(Cohort &cohort, AnalysisModule &module) {
    module.initialise_population(*cohort.context);
    return module.analyse(*cohort.context);
}

/// The spread of a sample about a mean given rather than computed from it, which is what every
/// `std_` column in this file is: the baseline divides by the head count and not by n−1.
double spread(const std::vector<double> &values, double mean, double denominator) {
    double sum = 0.0;
    for (const auto value : values) {
        sum += (value - mean) * (value - mean);
    }
    return std::sqrt(sum / denominator);
}

const std::vector<Income> &every_stratum() {
    static const std::vector<Income> strata{Income::low, Income::lowermiddle, Income::uppermiddle,
                                            Income::high};
    return strata;
}

} // namespace

TEST(AnalysisIncomeSeries, TheWeightCategoriesAreFilledPerStratum) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto result = analyse(cohort, module);
    const auto &series = result.series;
    ASSERT_TRUE(series.has_income_channels());

    double normal_total = 0.0;
    double above_total = 0.0;
    for (const auto income : every_stratum()) {
        for (const auto gender : {Gender::male, Gender::female}) {
            for (std::size_t age = 0; age <= static_cast<std::size_t>(kAgeUpper); ++age) {
                const auto count = stratified(series, gender, income, "count", age);
                const auto normal = stratified(series, gender, income, "normal_weight", age);
                const auto over = stratified(series, gender, income, "over_weight", age);
                const auto obese = stratified(series, gender, income, "obese_weight", age);
                const auto above = stratified(series, gender, income, "above_weight", age);

                // The same partition the whole-population series keeps, per stratum: everybody in
                // the band is in exactly one of the three, and `above` is the other two.
                EXPECT_DOUBLE_EQ(over + obese, above);
                EXPECT_DOUBLE_EQ(normal + over + obese, count);
            }

            normal_total += stratified(series, gender, income, "normal_weight");
            above_total += stratified(series, gender, income, "above_weight");
        }
    }

    // Two normal (21.0, 22.0) and three above it (27.0, 33.0, 31.0) among the five active people,
    // so the columns are populated rather than merely self-consistent — which zeros would be too.
    EXPECT_DOUBLE_EQ(2.0, normal_total);
    EXPECT_DOUBLE_EQ(3.0, above_total);
}

TEST(AnalysisIncomeSeries, EveryHeadCountSumsToTheWholePopulationsOwn) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto result = analyse(cohort, module);
    const auto &series = result.series;

    // `deaths` and `emigrations` are in this list and were not in the previous run's: they were
    // two of the 45, because this file skipped every inactive person outright and so never saw
    // anybody leave.
    for (const auto *channel : {"count", "deaths", "emigrations", "normal_weight", "over_weight",
                                "obese_weight", "above_weight"}) {
        for (const auto gender : {Gender::male, Gender::female}) {
            double total = 0.0;
            for (const auto income : every_stratum()) {
                total += stratified(series, gender, income, channel);
            }
            EXPECT_DOUBLE_EQ(series.at(gender, channel).at(kBand), total) << channel;
        }
    }
}

TEST(AnalysisIncomeSeries, SomebodyWhoDiedOrLeftIsCountedInTheStratumTheyWereIn) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto &series = analyse(cohort, module).series;

    EXPECT_DOUBLE_EQ(1.0, stratified(series, Gender::male, Income::low, "deaths"));
    EXPECT_DOUBLE_EQ(0.0, stratified(series, Gender::male, Income::low, "emigrations"));
    EXPECT_DOUBLE_EQ(0.0, stratified(series, Gender::female, Income::high, "deaths"));
    EXPECT_DOUBLE_EQ(1.0, stratified(series, Gender::female, Income::high, "emigrations"));

    // And they are not in the head count, which is the living.
    EXPECT_DOUBLE_EQ(3.0, stratified(series, Gender::male, Income::low, "count"));
    EXPECT_DOUBLE_EQ(2.0, stratified(series, Gender::female, Income::high, "count"));
}

TEST(AnalysisIncomeSeries, TheDemographicMeansAreTheStratumsOwn) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto &series = analyse(cohort, module).series;

    // low/male: regions 1, 3, 2; ethnicities 2, 2, 4; sectors urban, rural, urban (0, 1, 0);
    // continuous incomes 100, 200, 300; physical activity 1.6, 1.8, 2.0; BMI 21, 27, 33.
    EXPECT_DOUBLE_EQ(1.0, stratified(series, Gender::male, Income::low, "mean_gender"));
    EXPECT_DOUBLE_EQ(2.0, stratified(series, Gender::male, Income::low, "mean_region"));
    EXPECT_DOUBLE_EQ(8.0 / 3.0, stratified(series, Gender::male, Income::low, "mean_ethnicity"));
    EXPECT_DOUBLE_EQ(1.0 / 3.0, stratified(series, Gender::male, Income::low, "mean_sector"));
    EXPECT_DOUBLE_EQ(200.0, stratified(series, Gender::male, Income::low, "mean_income"));
    EXPECT_DOUBLE_EQ(1.8, stratified(series, Gender::male, Income::low,
                                     "mean_physical_activity"));
    EXPECT_DOUBLE_EQ(27.0, stratified(series, Gender::male, Income::low, "mean_bmi"));

    // `mean_income_category` is the stratum's own category, so it is constant within a file: 1 for
    // low, 4 for high. It is the column that says which file a reader is looking at.
    EXPECT_DOUBLE_EQ(1.0, stratified(series, Gender::male, Income::low, "mean_income_category"));
    EXPECT_DOUBLE_EQ(4.0, stratified(series, Gender::female, Income::high,
                                     "mean_income_category"));

    // high/female: the emigrant is not in any of these.
    EXPECT_DOUBLE_EQ(0.0, stratified(series, Gender::female, Income::high, "mean_gender"));
    EXPECT_DOUBLE_EQ(2.5, stratified(series, Gender::female, Income::high, "mean_region"));
    EXPECT_DOUBLE_EQ(2.0, stratified(series, Gender::female, Income::high, "mean_ethnicity"));
    EXPECT_DOUBLE_EQ(0.5, stratified(series, Gender::female, Income::high, "mean_sector"));
    EXPECT_DOUBLE_EQ(450.0, stratified(series, Gender::female, Income::high, "mean_income"));
    EXPECT_DOUBLE_EQ(1.7, stratified(series, Gender::female, Income::high,
                                     "mean_physical_activity"));
    EXPECT_DOUBLE_EQ(26.5, stratified(series, Gender::female, Income::high, "mean_bmi"));
}

TEST(AnalysisIncomeSeries, TheAgeColumnsAreTheRowsOwnKeyInEveryStratum) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto &series = analyse(cohort, module).series;

    // Every age of every configured stratum, including the two nobody is in and the sex nobody in
    // that stratum has. This is the whole of the `HLM_France` and `HLM_India` finding: their
    // stratum files are zero on both sides except for these three columns, which the baseline
    // fills because they are the row's own key rather than an average over its members.
    for (const auto income : every_stratum()) {
        for (const auto gender : {Gender::male, Gender::female}) {
            for (int age = 0; age <= kAgeUpper; ++age) {
                const auto band = static_cast<std::size_t>(age);
                const auto expected = static_cast<double>(age);
                EXPECT_DOUBLE_EQ(expected, stratified(series, gender, income, "mean_age", band));
                EXPECT_DOUBLE_EQ(expected * expected,
                                 stratified(series, gender, income, "mean_age2", band));
                EXPECT_DOUBLE_EQ(expected * expected * expected,
                                 stratified(series, gender, income, "mean_age3", band));
            }
        }
    }
}

TEST(AnalysisIncomeSeries, TheBurdenChannelsCountTheYearsDeadInTheirDenominator) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto &series = analyse(cohort, module).series;

    // low/male: one person died at 40 against a life expectancy of 80, so 40 years lost, and the
    // denominator is the three living plus the one dead. Years lived with disability come from
    // the two active asthmatics at 0.2 each.
    const double yll_sum = 40.0 * kDaly;
    const double yld_sum = 2.0 * kAsthmaBurden * kDaly;
    const double denominator = 3.0 + 1.0;

    EXPECT_DOUBLE_EQ(yll_sum / denominator,
                     stratified(series, Gender::male, Income::low, "mean_yll"));
    EXPECT_DOUBLE_EQ(yld_sum / denominator,
                     stratified(series, Gender::male, Income::low, "mean_yld"));
    EXPECT_DOUBLE_EQ((yll_sum + yld_sum) / denominator,
                     stratified(series, Gender::male, Income::low, "mean_daly"));

    // high/female: nobody died, so the denominator is the head count and years of life lost are
    // zero — which is not the same as the column being absent, and is why `std_yll` below is zero
    // here and not there.
    EXPECT_DOUBLE_EQ(0.0, stratified(series, Gender::female, Income::high, "mean_yll"));
    EXPECT_DOUBLE_EQ(1.0 * kAsthmaBurden * kDaly / 2.0,
                     stratified(series, Gender::female, Income::high, "mean_yld"));
}

TEST(AnalysisIncomeSeries, TheDiseaseRatesAreThePerStratumShare) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto &series = analyse(cohort, module).series;

    // Two of the three living low/male have asthma and one of them caught it this year.
    EXPECT_DOUBLE_EQ(2.0 / 3.0,
                     stratified(series, Gender::male, Income::low, "prevalence_asthma"));
    EXPECT_DOUBLE_EQ(1.0 / 3.0, stratified(series, Gender::male, Income::low, "incidence_asthma"));

    // One of the two living high/female has it, and caught it two years ago.
    EXPECT_DOUBLE_EQ(0.5, stratified(series, Gender::female, Income::high, "prevalence_asthma"));
    EXPECT_DOUBLE_EQ(0.0, stratified(series, Gender::female, Income::high, "incidence_asthma"));
}

TEST(AnalysisIncomeSeries, EveryStandardDeviationIsTheSpreadAboutTheStratumsOwnMean) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto &series = analyse(cohort, module).series;

    const double count = 3.0;

    // A mapping factor.
    EXPECT_DOUBLE_EQ(spread({21.0, 27.0, 33.0}, 27.0, count),
                     stratified(series, Gender::male, Income::low, "std_bmi"));

    // The demographics read off the person rather than out of the factor map.
    EXPECT_DOUBLE_EQ(spread({1.0, 3.0, 2.0}, 2.0, count),
                     stratified(series, Gender::male, Income::low, "std_region"));
    EXPECT_DOUBLE_EQ(spread({2.0, 2.0, 4.0}, 8.0 / 3.0, count),
                     stratified(series, Gender::male, Income::low, "std_ethnicity"));
    EXPECT_DOUBLE_EQ(spread({0.0, 1.0, 0.0}, 1.0 / 3.0, count),
                     stratified(series, Gender::male, Income::low, "std_sector"));
    EXPECT_DOUBLE_EQ(spread({100.0, 200.0, 300.0}, 200.0, count),
                     stratified(series, Gender::male, Income::low, "std_income"));

    // The dedicated physical-activity channel, which is a column of its own beside the mapping
    // factor of the same quantity.
    EXPECT_DOUBLE_EQ(spread({1.6, 1.8, 2.0}, 1.8, count),
                     stratified(series, Gender::male, Income::low, "std_physical_activity"));

    // The three burden channels, over the same denominator their means used — the living plus the
    // year's dead. Only the dead contribute to years of life lost and only the living to years
    // lived with disability, and both are divided by the larger figure.
    const double denominator = 4.0;
    const double burden = kAsthmaBurden * kDaly;
    const double mean_yll = 40.0 * kDaly / denominator;
    const double mean_yld = 2.0 * burden / denominator;
    const double mean_daly = (40.0 * kDaly + 2.0 * burden) / denominator;

    EXPECT_DOUBLE_EQ(spread({40.0 * kDaly}, mean_yll, denominator),
                     stratified(series, Gender::male, Income::low, "std_yll"));
    EXPECT_DOUBLE_EQ(spread({0.0, burden, burden}, mean_yld, denominator),
                     stratified(series, Gender::male, Income::low, "std_yld"));
    // Population order, and it matters to the last bit: the module accumulates in slot order, so
    // the three living come before the one who died and the sum rounds the way theirs does.
    EXPECT_DOUBLE_EQ(spread({0.0, burden, burden, 40.0 * kDaly}, mean_daly, denominator),
                     stratified(series, Gender::male, Income::low, "std_daly"));
}

TEST(AnalysisIncomeSeries, TheSpreadsWhoseMeansAreExactAreZeroRatherThanAbsent) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module);
    const auto &series = analyse(cohort, module).series;

    // `mean_age` is the row's own key and `mean_gender` is the file's own sex, so every person in
    // the band takes exactly the mean and the spread is zero by construction. `income_category` is
    // the same thing one level up: within a stratum file everybody has the stratum's category.
    //
    // They are zero in the baseline's stratum files too, which is why they were never in the 45 —
    // and asserting it here is what says so, rather than leaving four columns whose emptiness
    // nobody has explained.
    for (const auto *channel : {"std_age", "std_age2", "std_age3", "std_gender",
                                "std_income_category"}) {
        EXPECT_DOUBLE_EQ(0.0, stratified(series, Gender::male, Income::low, channel)) << channel;
        EXPECT_DOUBLE_EQ(0.0, stratified(series, Gender::female, Income::high, channel))
            << channel;
    }
}

TEST(AnalysisIncomeSeries, ADemographicThatIsAlsoADeclaredFactorHasItsSquareRootTakenTwice) {
    // The baseline's finishing loop walks every mapping entry and then a fixed list of demographic
    // names, so a name in both — `KevinHall_FINCH` declares `Region`, `Ethnicity`, `Income`,
    // `income_category`, `Age`, `Age2`, `Age3` and `Gender` as level-0 risk factors — is finished
    // twice and comes out as `sqrt(sqrt(Σd²/n)/n)`.
    //
    // This build reproduces it, in the whole-population series and here, because the equivalence
    // harness compares these two files column for column and a quiet correction would be a
    // difference nobody chose. It is recorded as an upstream finding
    // (docs/upstream-reports.md), and the evidence is in the baseline's own `KevinHall_FINCH`
    // output: a band of 50 people whose region has mean 1.38 reports `std_region` 0.122097, where
    // the spread is 0.745 and 0.122097 is its square root over 50.
    auto plain = make_cohort(cohort_specs());
    auto plain_module = make_module();
    configure(plain_module);
    const auto once = stratified(analyse(plain, plain_module).series, Gender::male, Income::low,
                                 "std_region");

    auto declared = make_cohort(cohort_specs(), /*region_is_a_declared_factor=*/true);
    auto declared_module = make_module();
    configure(declared_module);
    const auto twice = stratified(analyse(declared, declared_module).series, Gender::male,
                                  Income::low, "std_region");

    EXPECT_DOUBLE_EQ(spread({1.0, 3.0, 2.0}, 2.0, 3.0), once);
    EXPECT_DOUBLE_EQ(std::sqrt(once / 3.0), twice);
}

TEST(AnalysisIncomeSeries, NoStratifiedChannelsAtAllWhenIncomeAnalysisIsOff) {
    auto cohort = make_cohort(cohort_specs());
    auto module = make_module();
    configure(module, /*income_analysis=*/false);
    const auto result = analyse(cohort, module);
    EXPECT_FALSE(result.series.has_income_channels());

    // And the whole-population columns are unaffected by the switch.
    EXPECT_DOUBLE_EQ(5.0, result.series.at(Gender::male, "count").at(kBand) +
                              result.series.at(Gender::female, "count").at(kBand));
}
