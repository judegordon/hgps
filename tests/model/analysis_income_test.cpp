// The income-stratified series, driven directly over a small cohort.
//
// The first test this project has had of `calculate_income_based_series`, and it exists because
// that function left four columns of every stratum file empty for as long as it has existed,
// without anything noticing. The whole-population series is covered end to end by `tests/sim/simulation_test.cpp`
// over both fixture packs; the income series is not, because neither pack assigns an income
// category — both are HLM, and only the StaticLinear family assigns one — so no test that runs a
// configuration can reach this code at all. Hence a cohort built by hand.
//
// What found the defect was reading the baseline beside this file and then measuring: a
// `KevinHall_FINCH` run of each implementation puts 49 columns at identically zero in ours that
// the baseline fills (docs/backlog.md item 2). Four of them are the weight categories, fixed here.
#include "model/analysis/analysis_module.h"

#include "model/runtime_context.h"
#include "sim/scenario.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::core::Gender;
using hgps::core::Income;
using hgps::model::AnalysisDefinition;
using hgps::model::AnalysisModule;
using hgps::model::HierarchicalMapping;
using hgps::model::LmsDefinition;
using hgps::model::MappingEntry;
using hgps::model::RuntimeContext;
using hgps::model::WeightModel;

constexpr int kAgeUpper = 80;
constexpr int kYear = 2020;

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

AnalysisDefinition analysis_definition() {
    const hgps::core::IntegerInterval age_range{0, kAgeUpper};
    std::vector<int> years{kYear, kYear + 1};
    const std::vector<Gender> columns{Gender::male, Gender::female};
    hgps::core::Array2D<float> life_expectancy{years.size(), columns.size(), 80.0F};

    return AnalysisDefinition{
        hgps::model::GenderTable<int, float>{hgps::model::MonotonicVector<int>{years}, columns,
                                             std::move(life_expectancy)},
        hgps::model::create_age_gender_table<double>(age_range),
        std::map<hgps::core::Identifier, float>{}};
}

HierarchicalMapping mapping() {
    std::vector<MappingEntry> entries;
    entries.emplace_back("Gender", 0);
    entries.emplace_back("Age", 0);
    entries.emplace_back("BMI", 2);
    return HierarchicalMapping{std::move(entries)};
}

/// A context whose people are adults with a BMI and an income category.
struct Cohort {
    std::shared_ptr<const hgps::model::ModelInput> inputs;
    std::unique_ptr<RuntimeContext> context;
};

Cohort make_cohort(const std::vector<std::pair<Income, double>> &people) {
    hgps::config::ProjectRequirements requirements;
    requirements.income.enabled = true;
    requirements.income.categories = "4";
    requirements.income.income_based_csv_output = true;

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
        hgps::model::SesDefinition{.function_name = "normal", .parameters = {0.0, 1.0}}, mapping(),
        std::vector<hgps::core::DiseaseInfo>{}, requirements);

    cohort.context = std::make_unique<RuntimeContext>(
        cohort.inputs, std::make_unique<hgps::sim::BaselineScenario>(), 42U);
    cohort.context->start_run(1, 42U, people.size());
    cohort.context->set_current_time(kYear);

    std::size_t index = 0;
    for (const auto &[income, bmi] : people) {
        auto &person = cohort.context->population()[index];
        person.gender = (index % 2 == 0) ? Gender::male : Gender::female;
        person.age = 40;
        person.income = income;
        person.risk_factors[hgps::core::Identifier{"bmi"}] = bmi;
        ++index;
    }

    return cohort;
}

/// Normal, overweight and obese, by the adult cut-offs.
const std::vector<std::pair<Income, double>> &mixed_cohort() {
    static const std::vector<std::pair<Income, double>> people{
        {Income::low, 21.0},         {Income::low, 27.0},
        {Income::low, 34.0},         {Income::lowermiddle, 22.5},
        {Income::lowermiddle, 31.0}, {Income::uppermiddle, 26.0},
        {Income::uppermiddle, 19.0}, {Income::high, 33.0},
        {Income::high, 24.0},        {Income::high, 28.5},
    };
    return people;
}

/// The value of a stratified channel at one age, or zero when that (stratum, sex) never touched
/// it. The stratified channels are created on first use, so a stratum where nobody is obese has
/// no `obese_weight` vector at all — and the result writer reads them exactly this way, through
/// `DataSeries::find`, writing a zero for a channel that is not there.
double stratified(const hgps::model::DataSeries &series, Gender gender, Income income,
                  const std::string &channel, std::size_t age) {
    const auto *values = series.find(gender, income, channel);
    return values == nullptr ? 0.0 : values->at(age);
}

AnalysisModule make_module() {
    return AnalysisModule{analysis_definition(), WeightModel{child_reference()},
                          hgps::core::IntegerInterval{0, kAgeUpper}, 5U};
}

} // namespace

TEST(AnalysisIncomeSeries, TheWeightCategoriesAreFilledPerStratum) {
    auto cohort = make_cohort(mixed_cohort());
    auto module = make_module();
    module.set_income_analysis_enabled(true);
    module.initialise_population(*cohort.context);

    const auto result = module.analyse(*cohort.context);
    const auto &series = result.series;
    ASSERT_TRUE(series.has_income_channels());

    double normal_total = 0.0;
    double above_total = 0.0;
    for (const auto income : {Income::low, Income::lowermiddle, Income::uppermiddle,
                              Income::high}) {
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

            normal_total += stratified(series, gender, income, "normal_weight", 40);
            above_total += stratified(series, gender, income, "above_weight", 40);
        }
    }

    // Four normal (21.0, 22.5, 19.0, 24.0) and six above it, so the columns are populated rather
    // than merely self-consistent — which zeros would also be.
    EXPECT_DOUBLE_EQ(4.0, normal_total);
    EXPECT_DOUBLE_EQ(6.0, above_total);
}

TEST(AnalysisIncomeSeries, TheStrataSumToTheWholePopulationWhenEveryoneHasAnIncome) {
    auto cohort = make_cohort(mixed_cohort());
    auto module = make_module();
    module.set_income_analysis_enabled(true);
    module.initialise_population(*cohort.context);

    const auto result = module.analyse(*cohort.context);
    const auto &series = result.series;

    for (const auto *channel : {"count", "normal_weight", "over_weight", "obese_weight",
                                "above_weight"}) {
        for (const auto gender : {Gender::male, Gender::female}) {
            double total = 0.0;
            for (const auto income : {Income::low, Income::lowermiddle, Income::uppermiddle,
                                      Income::high}) {
                total += stratified(series, gender, income, channel, 40);
            }
            EXPECT_DOUBLE_EQ(series.at(gender, channel).at(40), total) << channel;
        }
    }
}

TEST(AnalysisIncomeSeries, NoStratifiedChannelsAtAllWhenIncomeAnalysisIsOff) {
    auto cohort = make_cohort(mixed_cohort());
    auto module = make_module();
    module.set_income_analysis_enabled(false);
    module.initialise_population(*cohort.context);

    const auto result = module.analyse(*cohort.context);
    EXPECT_FALSE(result.series.has_income_channels());

    // And the whole-population columns are unaffected by the switch.
    EXPECT_DOUBLE_EQ(10.0, result.series.at(Gender::male, "count").at(40) +
                               result.series.at(Gender::female, "count").at(40));
}
