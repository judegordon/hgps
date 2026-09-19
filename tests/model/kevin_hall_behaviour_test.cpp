// The Kevin Hall model driven over a small population, against the real converted FINCH model.
//
// These are the ports of the baseline's behavioural skips — `KevinHallHeight`'s
// `GenerateInitialisesHeightWithQuintileParams`, `UpdateChildrenRefreshesHeightForSubAdultAges`,
// `UpdateNewbornsInitialisesHeight` and `GenerateUsesBroadcastHeightWhenStratumNotAssigned`,
// `KevinHallWeightQuantiles.DifferentStrataCanProduceDifferentWeights`, and
// `KevinHallWeightValidation.WarnsAboveMaxAndThrowsBelowMinForConfiguredWeightRange`. All six skip
// upstream (audit B-11) and so have never run.
//
// The model is driven directly rather than through a whole simulation: what is being checked is
// its own behaviour, and a 6,800-person eleven-year run would take ten seconds to say less.
#include "model/riskfactor/kevin_hall/kevin_hall_model.h"

#include "config/models/model_loader.h"
#include "hgps/baseline_compat.h"
#include "core/string_util.h"
#include "io/csv_reader.h"
#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"
#include "sim/scenario.h"
#include "support/test_paths.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::config::Config;
using hgps::config::models::LoadContext;
using hgps::config::models::detail::load_kevin_hall;
using hgps::core::Gender;
using hgps::core::Identifier;
using hgps::diag::IssueReport;
using hgps::model::HierarchicalMapping;
using hgps::model::KevinHallModel;
using hgps::model::MappingEntry;
using hgps::model::Person;
using hgps::model::RuntimeContext;
using hgps::model::SexAgeFactorTable;
using json = nlohmann::json;

std::filesystem::path finch_dir() {
    return hgps::test::upstream_examples_dir() / "KevinHall_FINCH";
}

json finch_dynamic_model() {
    const auto path = finch_dir() / "dynamic_model.json";
    std::ifstream stream{path};
    EXPECT_TRUE(stream) << "the upstream FINCH example is not at " << path.string();
    return json::parse(stream);
}

const std::vector<std::string> &finch_foods() {
    static const std::vector<std::string> foods{
        "FoodCarbohydrate", "FoodProtein",      "FoodFat",
        "FoodSodium",       "FoodAlcohol",      "FoodLegume",
        "FoodVegetable",    "FoodFruit",        "FoodProcessedMeat",
        "FoodRedMeat",      "FoodFibre",        "FoodTotalSugar",
        "FoodAddedSugar",   "FoodSaturatedFat", "FoodPolyunsaturatedFattyAcid",
        "FoodMonounsaturatedFat", "FoodIron",   "FoodCalcium",
        "FoodVitaminC",     "FoodCopper",       "FoodZinc"};
    return foods;
}

/// A weight range wide enough that a newborn's weight is inside it, unless a test narrows it.
HierarchicalMapping finch_mapping(double weight_lower = 1.0, double weight_upper = 210.0) {
    std::vector<MappingEntry> entries{
        MappingEntry{"Age", 0},
        MappingEntry{"Gender", 0},
        MappingEntry{"EnergyIntake", 2,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{0.3, 19072.8}}},
        MappingEntry{"PhysicalActivity", 2,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{1.4, 2.5}}},
        MappingEntry{"Weight", 3,
                     hgps::model::OptionalInterval{
                         hgps::core::DoubleInterval{weight_lower, weight_upper}}},
        MappingEntry{"Height", 3,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{44.0, 203.2}}},
        MappingEntry{"BMI", 3,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{10.0, 60.0}}},
    };
    return HierarchicalMapping{std::move(entries)};
}

/// The real FINCH FactorsMean tables.
///
/// Invented flat values do not work here and the reason is worth stating: the model derives an
/// adult's expected weight from their expected energy intake, height, age and activity level, and
/// those four are not independent. A table with a plausible height and an implausible energy
/// intake produces a *negative* expected weight, which the model then rightly refuses. Using the
/// pack's own tables keeps a test about height from being a test of arithmetic nobody meant.
std::shared_ptr<SexAgeFactorTable> finch_expected() {
    static const auto table = [] {
        auto built = std::make_shared<SexAgeFactorTable>();
        IssueReport report;
        for (const auto &[sex, file] : std::vector<std::pair<Gender, std::string>>{
                 {Gender::male, "Finch.FactorsMean.Male.csv"},
                 {Gender::female, "Finch.FactorsMean.Female.csv"}}) {
            const auto values = hgps::io::load_baseline_adjustments_from_csv(
                finch_dir() / file, hgps::io::CsvOptions{}, report);
            EXPECT_TRUE(values.has_value()) << report.to_string();
            if (values.has_value()) {
                for (const auto &[factor, by_age] : *values) {
                    built->emplace(sex, factor, by_age);
                }
            }
        }
        return built;
    }();
    return table;
}

Config finch_config(bool stratified = true, std::size_t strata = 5) {
    Config config;
    config.root_path = finch_dir();
    config.settings.age_range = hgps::core::IntegerInterval{0, 110};
    config.modelling.baseline_adjustments.income_stratum_factors_mean.enabled = stratified;
    config.modelling.baseline_adjustments.income_stratum_factors_mean
        .adjustment_income_stratum_count = strata;
    config.project_requirements.income.type = "continuous";
    config.project_requirements.income.categories = "4";
    return config;
}

std::unique_ptr<hgps::model::RiskFactorModel>
load_model(const json &document, const Config &config, const HierarchicalMapping &mapping,
           const std::shared_ptr<SexAgeFactorTable> &expected, IssueReport &report) {
    std::vector<Identifier> extra;
    for (const auto &food : finch_foods()) {
        extra.emplace_back(food);
    }

    const LoadContext context{.mapping = &mapping,
                              .expected = expected,
                              .config = &config,
                              .trend = nullptr,
                              .trend_steps = nullptr,
                              .extra_factors = std::move(extra)};
    return load_kevin_hall(document, finch_dir() / "dynamic_model.json", context, report);
}

/// What the static model would have left on a person: every food group, a physical activity level
/// and an income adjustment stratum.
void seed_person(Person &person, double food = 50.0) {
    for (const auto &name : finch_foods()) {
        person.risk_factors[Identifier{hgps::core::to_lower(name)}] = food;
    }
    person.risk_factors[Identifier{"physicalactivity"}] = 1.6;
    person.physical_activity = 1.6;
}

/// A context over a cohort of `count` people, all of one sex, spread over the ages given.
struct Harness {
    std::shared_ptr<const hgps::model::ModelInput> inputs;
    std::unique_ptr<RuntimeContext> context;
    hgps::sim::ScenarioJournal journal;
};

Harness make_harness(const HierarchicalMapping &mapping, const std::vector<unsigned int> &ages,
                     std::size_t per_age, int year = 2022) {
    hgps::model::Settings settings{
        .country = hgps::core::Country{.code = 826,
                                        .name = "United Kingdom",
                                        .alpha2 = "GB",
                                        .alpha3 = "GBR"},
        .size_fraction = 0.0001,
        .age_range = hgps::core::IntegerInterval{0, 110}};

    hgps::model::RunInfo run{.start_time = static_cast<unsigned int>(year),
                             .stop_time = static_cast<unsigned int>(year) + 5,
                             .seed = 42};

    Harness harness;
    harness.inputs = std::make_shared<const hgps::model::ModelInput>(
        hgps::core::DataTable{}, settings, run,
        hgps::model::SesDefinition{.function_name = "normal", .parameters = {0.0, 1.0}}, mapping,
        std::vector<hgps::core::DiseaseInfo>{}, hgps::config::ProjectRequirements{});

    harness.context = std::make_unique<RuntimeContext>(
        harness.inputs, std::make_unique<hgps::sim::BaselineScenario>(), 42U);
    harness.context->start_run(1, 42U, ages.size() * per_age);
    harness.context->set_current_time(year);

    std::size_t index = 0;
    for (const auto age : ages) {
        for (std::size_t i = 0; i < per_age; ++i) {
            auto &person = harness.context->population()[index++];
            person.gender = Gender::male;
            person.age = age;
            seed_person(person);
        }
    }

    return harness;
}

double height_of(const Person &person) {
    return person.risk_factors.at(Identifier{"height"});
}

double weight_of(const Person &person) {
    return person.risk_factors.at(Identifier{"weight"});
}

} // namespace

TEST(KevinHallBehaviour, GenerateGivesEveryoneAWeightAHeightAndABmi) {
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    IssueReport report;
    auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
    ASSERT_NE(nullptr, model) << report.to_string();

    auto harness = make_harness(mapping, {0, 5, 12, 25, 40, 70}, 6);
    model->generate_risk_factors(*harness.context, harness.journal);

    for (const auto &person : harness.context->population()) {
        ASSERT_TRUE(person.risk_factors.contains(Identifier{"weight"}));
        ASSERT_TRUE(person.risk_factors.contains(Identifier{"height"}));
        ASSERT_TRUE(person.risk_factors.contains(Identifier{"bmi"}));
        ASSERT_TRUE(person.risk_factors.contains(Identifier{"bodyfat"}));
        ASSERT_TRUE(person.risk_factors.contains(Identifier{"leantissue"}));

        EXPECT_GT(weight_of(person), 0.0);
        EXPECT_GT(height_of(person), 0.0);
        EXPECT_GT(person.risk_factors.at(Identifier{"bmi"}), 0.0);

        // Body fat plus lean tissue plus the water compartments is the weight, which is what makes
        // the energy balance's bookkeeping closed.
        const double parts = person.risk_factors.at(Identifier{"bodyfat"}) +
                             person.risk_factors.at(Identifier{"leantissue"}) +
                             person.risk_factors.at(Identifier{"glycogen"}) * 3.7 +
                             person.risk_factors.at(Identifier{"extracellularfluid"});
        EXPECT_NEAR(weight_of(person), parts, 1e-9);
    }
}

TEST(KevinHallBehaviour, HeightUsesTheParametersOfThePersonsOwnIncomeQuintile) {
    // The baseline's KevinHallHeight.GenerateInitialisesHeightWithQuintileParams. The five
    // quintile rows of height_male.csv have different slopes, so two people with the same weight
    // and different strata must get different heights.
    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    IssueReport report;
    auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
    ASSERT_NE(nullptr, model) << report.to_string();

    auto harness = make_harness(mapping, {40}, 10);
    // Two strata, alternating, so both are populated and each has the same weights.
    for (std::size_t i = 0; i < harness.context->population().size(); ++i) {
        auto &person = harness.context->population()[i];
        person.has_income_adjustment_stratum = true;
        person.income_adjustment_stratum = i % 2 == 0 ? 0 : 4;
    }

    model->generate_risk_factors(*harness.context, harness.journal);

    auto *kevin_hall = dynamic_cast<KevinHallModel *>(model.get());
    ASSERT_NE(nullptr, kevin_hall);
    EXPECT_NE(kevin_hall->height_params_for(harness.context->population()[0]).slope,
              kevin_hall->height_params_for(harness.context->population()[1]).slope);

    // Every person's height is positive and finite; the two strata's means differ.
    double lowest = 0.0;
    double highest = 0.0;
    int count = 0;
    for (std::size_t i = 0; i < harness.context->population().size(); ++i) {
        const auto &person = harness.context->population()[i];
        EXPECT_GT(height_of(person), 0.0);
        (i % 2 == 0 ? lowest : highest) += height_of(person);
        ++count;
    }
    EXPECT_GT(count, 0);
    EXPECT_NE(lowest, highest);
}

TEST(KevinHallBehaviour, WithoutAStratumEverybodyUsesTheFirstRow) {
    // The baseline's KevinHallHeight.GenerateUsesBroadcastHeightWhenStratumNotAssigned.
    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    IssueReport report;
    auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
    ASSERT_NE(nullptr, model) << report.to_string();

    auto *kevin_hall = dynamic_cast<KevinHallModel *>(model.get());
    ASSERT_NE(nullptr, kevin_hall);

    Person unassigned;
    unassigned.gender = Gender::male;
    unassigned.age = 40;

    Person first_stratum = unassigned;
    first_stratum.has_income_adjustment_stratum = true;
    first_stratum.income_adjustment_stratum = 0;

    EXPECT_DOUBLE_EQ(kevin_hall->height_params_for(first_stratum).slope,
                     kevin_hall->height_params_for(unassigned).slope);
    EXPECT_EQ(kevin_hall->weight_quantiles_for(first_stratum),
              kevin_hall->weight_quantiles_for(unassigned));
}

TEST(KevinHallBehaviour, DifferentStrataProduceDifferentWeights) {
    // The baseline's KevinHallWeightQuantiles.DifferentStrataCanProduceDifferentWeights.
    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    IssueReport report;
    auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
    ASSERT_NE(nullptr, model) << report.to_string();

    auto harness = make_harness(mapping, {40}, 2);
    harness.context->population()[0].has_income_adjustment_stratum = true;
    harness.context->population()[0].income_adjustment_stratum = 0;
    harness.context->population()[1].has_income_adjustment_stratum = true;
    harness.context->population()[1].income_adjustment_stratum = 4;

    // Both people have identical food intakes, so any difference in weight is the quantile curve.
    model->generate_risk_factors(*harness.context, harness.journal);

    auto *kevin_hall = dynamic_cast<KevinHallModel *>(model.get());
    EXPECT_NE(kevin_hall->weight_quantiles_for(harness.context->population()[0]),
              kevin_hall->weight_quantiles_for(harness.context->population()[1]));
}

TEST(KevinHallBehaviour, AChildsHeightIsRefreshedEachYearAndAnAdultsIsNot) {
    // The baseline's KevinHallHeight.UpdateChildrenRefreshesHeightForSubAdultAges. Height tracks
    // weight while a person is growing and is fixed once they are not, which is why
    // `update_others` only calls `update_height` below the Kevin Hall minimum age.
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    IssueReport report;
    auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
    ASSERT_NE(nullptr, model) << report.to_string();

    auto harness = make_harness(mapping, {10, 40}, 8);
    model->generate_risk_factors(*harness.context, harness.journal);

    const double child_before = height_of(harness.context->population()[0]);
    const double adult_before = height_of(harness.context->population()[8]);

    // A year in which everybody's food intake rises, so weight moves and height would follow it.
    harness.context->set_current_time(2023);
    harness.journal.reset_adjustment_cursor(1, 2023);
    for (auto &person : harness.context->population()) {
        ++person.age;
        seed_person(person, 65.0);
    }
    model->update_risk_factors(*harness.context, harness.journal);

    EXPECT_NE(child_before, height_of(harness.context->population()[0]))
        << "a child's height should follow their weight";
    EXPECT_DOUBLE_EQ(adult_before, height_of(harness.context->population()[8]))
        << "an adult's height should not change";
}

TEST(KevinHallBehaviour, ANewbornIsGivenAWeightAHeightAndAState) {
    // The baseline's KevinHallHeight.UpdateNewbornsInitialisesHeight.
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    IssueReport report;
    auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
    ASSERT_NE(nullptr, model) << report.to_string();

    auto harness = make_harness(mapping, {30}, 8);
    model->generate_risk_factors(*harness.context, harness.journal);

    harness.context->set_current_time(2023);
    harness.journal.reset_adjustment_cursor(1, 2023);
    for (auto &person : harness.context->population()) {
        ++person.age;
    }
    harness.context->population().add_newborn_babies(4, Gender::male, 2023);
    for (auto &person : harness.context->population()) {
        if (person.age == 0) {
            seed_person(person);
        }
    }

    model->update_risk_factors(*harness.context, harness.journal);

    int newborns = 0;
    for (const auto &person : harness.context->population()) {
        if (person.age != 0) {
            continue;
        }
        ++newborns;
        ASSERT_TRUE(person.risk_factors.contains(Identifier{"height"}));
        ASSERT_TRUE(person.risk_factors.contains(Identifier{"height_residual"}));
        ASSERT_TRUE(person.risk_factors.contains(Identifier{"intercept_k"}));
        EXPECT_GT(height_of(person), 0.0);
        EXPECT_GT(weight_of(person), 0.0);
    }
    EXPECT_EQ(4, newborns);
}

TEST(KevinHallBehaviour, AWeightBelowTheConfiguredMinimumStopsTheRunAndAboveTheMaximumIsCounted) {
    // The baseline's KevinHallWeightValidation.WarnsAboveMaxAndThrowsBelowMinForConfiguredWeightRange.
    // Below the minimum the BMI, the weight classification and the disease models all stop meaning
    // anything, so it is an error; above it the person is implausible but describable, so the run
    // continues and says how often it happened.
    const auto config = finch_config();
    IssueReport report;

    {
        // A minimum nobody can meet.
        const auto mapping = finch_mapping(500.0, 900.0);
        auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
        ASSERT_NE(nullptr, model) << report.to_string();

        auto harness = make_harness(mapping, {40}, 4);
        EXPECT_THROW(model->generate_risk_factors(*harness.context, harness.journal),
                     hgps::diag::InternalError);
    }

    {
        // A maximum nobody can stay under.
        const auto mapping = finch_mapping(0.001, 0.002);
        auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
        ASSERT_NE(nullptr, model) << report.to_string();

        auto harness = make_harness(mapping, {40}, 4);
        EXPECT_NO_THROW(model->generate_risk_factors(*harness.context, harness.journal));
        ASSERT_TRUE(harness.context->metrics().contains("WeightAboveConfiguredMaximum"));
        EXPECT_GT(harness.context->metrics().at("WeightAboveConfiguredMaximum"), 0.0);
    }
}

TEST(KevinHallBehaviour, AYearOfStarvationStopsAtNoBodyFatRatherThanGoingThroughIt) {
    // The guard, driven through the real model rather than through its arithmetic
    // (ADR 0049, docs/findings/seed-80.md). A cohort is initialised in balance, and then their
    // food intake collapses to a fortieth of what it was while their physical activity goes to
    // the top of its configured range. That is the shape of the draw seed 80 produced: a steady
    // state with negative body fat, reached inside one year.
    //
    // Without the guard the run leaves people with a negative body fat mass, which puts the
    // partition coefficient past its pole and the year after that overflows. With it, they stop
    // at no fat, the run says who and when, and every number stays describable.
    const auto starve = [](bool unbounded) {
        const auto config = [unbounded] {
            auto built = finch_config();
            if (unbounded) {
                built.baseline_compat.set(hgps::api::CompatFlag::b29);
            }
            return built;
        }();
        const auto mapping = finch_mapping();
        IssueReport report;
        auto model = load_model(finch_dynamic_model(), config, mapping, finch_expected(), report);
        EXPECT_NE(nullptr, model) << report.to_string();

        auto harness = make_harness(mapping, {40}, 12);
        model->generate_risk_factors(*harness.context, harness.journal);

        harness.context->set_current_time(2023);
        for (auto &person : harness.context->population()) {
            seed_person(person, 1.25);
            person.risk_factors[Identifier{"physicalactivity"}] = 2.5;
            person.physical_activity = 2.5;
        }
        model->update_risk_factors(*harness.context, harness.journal);
        return harness;
    };

    {
        auto harness = starve(false);

        // Nobody has a negative body fat mass, and nobody is past the pole.
        constexpr double pole = -10.4 * KevinHallModel::kRhoLean / KevinHallModel::kRhoFat;
        int at_the_boundary = 0;
        for (const auto &person : harness.context->population()) {
            const double fat = person.risk_factors.at(Identifier{"bodyfat"});
            EXPECT_GE(fat, 0.0) << "person " << person.id();
            EXPECT_GT(fat, pole);
            EXPECT_TRUE(std::isfinite(weight_of(person)));
            EXPECT_GT(weight_of(person), 0.0);
            if (fat == 0.0) {
                ++at_the_boundary;
            }
        }
        ASSERT_GT(at_the_boundary, 0) << "the starvation was not severe enough to reach the "
                                         "boundary, so this test is checking nothing";

        // Counted, and located: who, when, and what the step would have produced.
        ASSERT_TRUE(harness.context->metrics().contains("EnergyBalanceBodyFatBounded"));
        EXPECT_EQ(static_cast<double>(at_the_boundary),
                  harness.context->metrics().at("EnergyBalanceBodyFatBounded"));

        const auto &warnings = harness.context->warnings();
        EXPECT_EQ(static_cast<std::size_t>(at_the_boundary), warnings.total());
        ASSERT_FALSE(warnings.kept().empty());
        const auto &first = warnings.kept().front();
        EXPECT_EQ("energy_balance_body_fat_bounded", first.code);
        EXPECT_EQ(2023, first.year);
        EXPECT_NE(0U, first.person);
        EXPECT_NE(std::string::npos, first.message.find("body fat mass"));
    }

    {
        // B-29 on: the baseline's behaviour is back, bug and all.
        auto harness = starve(true);

        int negative = 0;
        for (const auto &person : harness.context->population()) {
            if (person.risk_factors.at(Identifier{"bodyfat"}) < 0.0) {
                ++negative;
            }
        }
        EXPECT_GT(negative, 0) << "with the flag on the energy balance should integrate straight "
                                  "through zero, as the baseline does";
        EXPECT_FALSE(harness.context->metrics().contains("EnergyBalanceBodyFatBounded"));
        EXPECT_TRUE(harness.context->warnings().empty());
    }
}
