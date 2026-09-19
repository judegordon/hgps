// The last gate before a number becomes output: nothing impossible reaches a result file.
//
// Every model in this program has its own guards, and the energy balance gained one this run
// (ADR 0049). This is the check that does not depend on any of them being right: the analysis
// path is where a person's value becomes a mean, it is the last place that still knows *whose*
// value it is, and it refuses to carry one that is not a number or not a description of a person
// (ADR 0050).
//
// The baseline replaces a NaN with zero in the same loop and says nothing
// (`analysis_module.cpp:388`), so an impossible value becomes a quietly wrong mean; it does not
// look at an infinity at all. That is deviation B-30, and the flag that restores it is tested
// here beside the guard, because "what does the baseline do instead?" is the question a reader
// of a deviation asks first.
#include "model/analysis/analysis_module.h"

#include "diagnostics/internal_error.h"
#include "hgps/baseline_compat.h"
#include "model/runtime_context.h"
#include "sim/scenario.h"

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::core::Gender;
using hgps::core::Identifier;
using hgps::model::AnalysisDefinition;
using hgps::model::AnalysisModule;
using hgps::model::HierarchicalMapping;
using hgps::model::LmsDefinition;
using hgps::model::MappingEntry;
using hgps::model::RuntimeContext;
using hgps::model::WeightModel;

constexpr int kAgeUpper = 80;
constexpr int kYear = 2020;

LmsDefinition child_reference() {
    hgps::model::LmsDataset dataset;
    for (unsigned int age = 0; age <= 18; ++age) {
        dataset[age][Gender::male] =
            hgps::model::LmsRecord{.lambda = -1.0, .mu = 16.0, .sigma = 0.1};
        dataset[age][Gender::female] =
            hgps::model::LmsRecord{.lambda = -1.0, .mu = 16.0, .sigma = 0.1};
    }
    return LmsDefinition{std::move(dataset)};
}

AnalysisDefinition analysis_definition() {
    const hgps::core::IntegerInterval age_range{0, kAgeUpper};
    const std::vector<int> years{kYear, kYear + 1};
    const std::vector<Gender> columns{Gender::male, Gender::female};
    hgps::core::Array2D<float> life_expectancy{years.size(), columns.size(), 80.0F};

    return AnalysisDefinition{
        hgps::model::GenderTable<int, float>{hgps::model::MonotonicVector<int>{years}, columns,
                                             std::move(life_expectancy)},
        hgps::model::create_age_gender_table<double>(age_range),
        std::map<Identifier, float>{}};
}

/// Weight, height, BMI and energy intake: the four factors with a bound this code can state,
/// plus one — physical activity — that has none, so the finiteness half of the check is tested
/// on a factor the bounds table says nothing about.
HierarchicalMapping invariant_mapping() {
    std::vector<MappingEntry> entries;
    entries.emplace_back("Gender", 0);
    entries.emplace_back("Age", 0);
    entries.emplace_back("PhysicalActivity", 2);
    entries.emplace_back("EnergyIntake", 2);
    entries.emplace_back("Weight", 3);
    entries.emplace_back("Height", 3);
    entries.emplace_back("BMI", 3);
    return HierarchicalMapping{std::move(entries)};
}

struct Cohort {
    std::shared_ptr<const hgps::model::ModelInput> inputs;
    std::unique_ptr<RuntimeContext> context;
};

/// Three ordinary adults, every value well inside every bound. `omit` names a factor the middle
/// one is simply never given — `FactorValues` has no `erase`, and a person who never had a value
/// is the case that actually occurs: not every project assigns every declared factor.
Cohort make_cohort(const char *omit = nullptr) {
    const hgps::model::Settings settings{
        .country = hgps::core::Country{.code = 826,
                                       .name = "United Kingdom",
                                       .alpha2 = "GB",
                                       .alpha3 = "GBR"},
        .size_fraction = 1.0,
        .age_range = hgps::core::IntegerInterval{0, kAgeUpper}};
    const hgps::model::RunInfo run{.start_time = kYear, .stop_time = kYear + 1, .seed = 42};

    Cohort cohort;
    cohort.inputs = std::make_shared<const hgps::model::ModelInput>(
        hgps::core::DataTable{}, settings, run,
        hgps::model::SesDefinition{.function_name = "normal", .parameters = {0.0, 1.0}},
        invariant_mapping(), std::vector<hgps::core::DiseaseInfo>{},
        hgps::config::ProjectRequirements{});

    cohort.context = std::make_unique<RuntimeContext>(
        cohort.inputs, std::make_unique<hgps::sim::BaselineScenario>(), 42U);
    cohort.context->start_run(1, 42U, 3);
    cohort.context->set_current_time(kYear);

    for (std::size_t index = 0; index < 3; ++index) {
        auto &person = cohort.context->population()[index];
        person.gender = index == 0 ? Gender::male : Gender::female;
        person.age = 40;
        for (const auto &[name, value] :
             std::vector<std::pair<const char *, double>>{{"physicalactivity", 1.6},
                                                          {"energyintake", 10000.0},
                                                          {"weight", 80.0},
                                                          {"height", 175.0},
                                                          {"bmi", 26.1}}) {
            if (index == 1 && omit != nullptr && std::string{omit} == name) {
                continue;
            }
            person.risk_factors[Identifier{name}] = value;
        }
    }
    return cohort;
}

std::unique_ptr<AnalysisModule> make_module(bool substitutes = false) {
    auto module = std::make_unique<AnalysisModule>(
        analysis_definition(), WeightModel{child_reference()},
        hgps::core::IntegerInterval{0, kAgeUpper}, 2);
    module->set_income_analysis_enabled(false);
    module->set_substitutes_impossible_values(substitutes);
    return module;
}

/// Sets one factor on person index 1 — the middle of the three, so a test that passes by
/// stopping at the first or last person would not.
void injure(Cohort &cohort, const char *factor, double value) {
    cohort.context->population()[1].risk_factors[Identifier{factor}] = value;
}

} // namespace

TEST(AnalysisInvariants, AnOrdinaryCohortPassesAndReportsItsMeans) {
    auto cohort = make_cohort();
    auto module = make_module();
    module->initialise_population(*cohort.context);

    const auto result = module->analyse(*cohort.context);
    EXPECT_DOUBLE_EQ(80.0, result.risk_factor_average.at("Weight").male);
    EXPECT_DOUBLE_EQ(80.0, result.risk_factor_average.at("Weight").female);
    EXPECT_DOUBLE_EQ(10000.0, result.risk_factor_average.at("EnergyIntake").male);
}

TEST(AnalysisInvariants, ADeliberatelyInjectedNotANumberIsCaughtAndLocated) {
    // The test the guard exists for. Before this run the same value was silently replaced with
    // zero, here and upstream, and the year's mean weight was then wrong by a third with nothing
    // in the output saying so.
    auto cohort = make_cohort();
    auto module = make_module();
    module->initialise_population(*cohort.context);

    injure(cohort, "weight", std::numeric_limits<double>::quiet_NaN());

    try {
        (void)module->analyse(*cohort.context);
        FAIL() << "a NaN weight reached the results";
    } catch (const hgps::diag::InternalError &caught) {
        const std::string message = caught.what();
        // Located: who, when, and which term. A message with any one of the three missing is a
        // message somebody cannot act on.
        EXPECT_NE(std::string::npos, message.find("person 2")) << message;
        EXPECT_NE(std::string::npos, message.find("2020")) << message;
        EXPECT_NE(std::string::npos, message.find("Weight")) << message;
        EXPECT_NE(std::string::npos, message.find("female")) << message;
        EXPECT_NE(std::string::npos, message.find("age 40")) << message;
    }
}

TEST(AnalysisInvariants, AnInfinityIsCaughtToo) {
    // The half the baseline does not have at all: it tests for NaN and an infinity goes straight
    // through into the sum, where it makes the whole band's mean infinite.
    for (const double value : {std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity()}) {
        auto cohort = make_cohort();
        auto module = make_module();
        module->initialise_population(*cohort.context);
        injure(cohort, "physicalactivity", value);

        EXPECT_THROW(module->analyse(*cohort.context), hgps::diag::InternalError)
            << "value " << value;
    }
}

TEST(AnalysisInvariants, AnImpossibleButFiniteValueIsCaughtForTheFactorsThatHaveABound) {
    // Finiteness is not enough: -1.7e283 kg is a perfectly finite double, and it is the number
    // seed 80 of `KevinHall_FINCH` produced (docs/findings/seed-80.md).
    const std::vector<std::pair<const char *, double>> impossible{
        {"weight", -1.7019180456941046e+283},
        {"weight", -5.0},
        {"weight", 0.0},
        {"weight", 5000.0},
        {"height", -10.0},
        {"bmi", -1.0},
        {"energyintake", -1.0},
    };
    for (const auto &[factor, value] : impossible) {
        auto cohort = make_cohort();
        auto module = make_module();
        module->initialise_population(*cohort.context);
        injure(cohort, factor, value);

        EXPECT_THROW(module->analyse(*cohort.context), hgps::diag::InternalError)
            << factor << " = " << value;
    }
}

TEST(AnalysisInvariants, AFactorWithNoStatedBoundIsCheckedForFinitenessOnly) {
    // `PhysicalActivity` has no ceiling this code can defend, so inventing one would make the
    // guard a modelling opinion. An absurd but finite value passes, and that is deliberate.
    auto cohort = make_cohort();
    auto module = make_module();
    module->initialise_population(*cohort.context);
    injure(cohort, "physicalactivity", 1.0e9);

    EXPECT_NO_THROW(module->analyse(*cohort.context));
}

TEST(AnalysisInvariants, AnAbsentFactorStillContributesNothingRatherThanFailing) {
    // Not every project assigns every declared factor, and a factor nobody has is not an
    // impossible value. This is the behaviour the guard had to leave alone: the accumulation
    // skips the person and the mean is still over the whole head count, which is what it has
    // always been.
    //
    // `EnergyIntake` rather than `BMI`, because the weight classifier reads BMI for every person
    // and rightly refuses a cohort missing one — a different check, in a different place, and
    // not the one this test is about.
    auto cohort = make_cohort("energyintake");
    auto module = make_module();
    module->initialise_population(*cohort.context);

    const auto result = module->analyse(*cohort.context);
    // Two women, one of whom has no energy intake at all: 10,000 over two.
    EXPECT_DOUBLE_EQ(5000.0, result.risk_factor_average.at("EnergyIntake").female);
    EXPECT_DOUBLE_EQ(10000.0, result.risk_factor_average.at("EnergyIntake").male);
}

TEST(AnalysisInvariants, WithTheB30FlagOnTheBaselineSubstitutionIsBackAndMeasurable) {
    // B-30: a NaN becomes zero and the run carries on, which is what the baseline does. The
    // point of the flag is that the *size* of the difference is a measurement — here, a mean
    // weight of 40 where the truth is undefined.
    auto cohort = make_cohort();
    auto module = make_module(true);
    module->initialise_population(*cohort.context);
    injure(cohort, "weight", std::numeric_limits<double>::quiet_NaN());

    const auto result = module->analyse(*cohort.context);
    EXPECT_DOUBLE_EQ(40.0, result.risk_factor_average.at("Weight").female);
    EXPECT_DOUBLE_EQ(80.0, result.risk_factor_average.at("Weight").male);
}
