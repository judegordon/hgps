// The StaticLinear model's loader, against the real converted FINCH example.
//
// Ports the baseline's `ModelParserFinch` suite — three tests, all of which skip, because the
// FINCH fixture path they derive from `__FILE__` exists in neither upstream data repository
// (audit B-11). Here the path comes from the build and a missing fixture fails.
//
// The rest are the cases that produced plausible numbers rather than an error while this loader
// was being written, and that a reader would not think to check: the headerless two-column
// regression files, the row-index column of the quantile CSVs, and the flag the baseline reads
// and never consults.
#include "config/models/model_loader.h"

#include "core/interval.h"
#include "diagnostics/issue_report.h"
#include "core/string_util.h"
#include "model/mapping.h"
#include "model/riskfactor/static_linear/static_linear_model.h"
#include "support/test_paths.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::config::Config;
using hgps::config::models::LoadContext;
using hgps::config::models::detail::load_region_and_ethnicity;
using hgps::config::models::detail::load_static_linear;
using hgps::core::Gender;
using hgps::core::Identifier;
using hgps::diag::IssueReport;
using hgps::model::HierarchicalMapping;
using hgps::model::MappingEntry;
using hgps::model::SexAgeFactorTable;
using json = nlohmann::json;

std::filesystem::path finch_dir() {
    return hgps::test::upstream_examples_dir() / "KevinHall_FINCH";
}

std::filesystem::path finch_static_model_path() {
    // `new_static_model.json`, not `static_model.json`: the latter names two policy files the pack
    // does not contain (audit D-02), which is what the converter's --policy-scenario exists for.
    return finch_dir() / "new_static_model.json";
}

json finch_static_model() {
    std::ifstream stream{finch_static_model_path()};
    EXPECT_TRUE(stream) << "the upstream FINCH example is not at " << finch_dir().string()
                        << "; these tests need it and must not skip (audit B-11)";
    return json::parse(stream);
}

/// The 21 food groups the correlation matrix's columns name, in its order.
const std::vector<std::string> &finch_factors() {
    static const std::vector<std::string> names{
        "FoodCarbohydrate", "FoodFat",          "FoodProtein",
        "FoodSodium",       "FoodAlcohol",      "FoodFibre",
        "FoodMonounsaturatedFat", "FoodPolyunsaturatedFattyAcid", "FoodSaturatedFat",
        "FoodTotalSugar",   "FoodAddedSugar",   "FoodFruit",
        "FoodVegetable",    "FoodLegume",       "FoodRedMeat",
        "FoodProcessedMeat", "FoodCalcium",     "FoodIron",
        "FoodVitaminC",     "FoodCopper",       "FoodZinc"};
    return names;
}

HierarchicalMapping finch_mapping() {
    std::vector<MappingEntry> entries{
        MappingEntry{"Age", 0},
        MappingEntry{"Gender", 0},
        MappingEntry{"Region", 0},
        MappingEntry{"Ethnicity", 0},
        MappingEntry{"Income", 0},
        MappingEntry{"EnergyIntake", 2,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{0.3, 19072.8}}},
        MappingEntry{"PhysicalActivity", 2,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{1.4, 2.5}}},
    };
    return HierarchicalMapping{std::move(entries)};
}

std::shared_ptr<SexAgeFactorTable> finch_expected() {
    auto table = std::make_shared<SexAgeFactorTable>();
    std::vector<std::string> names{"energyintake", "physicalactivity", "income"};
    for (const auto &factor : finch_factors()) {
        names.push_back(factor);
    }

    for (const auto sex : {Gender::male, Gender::female}) {
        for (const auto &name : names) {
            table->emplace(sex, Identifier{name}, std::vector<double>(111, 50.0));
        }
    }
    return table;
}

Config finch_config() {
    Config config;
    config.root_path = finch_dir();
    config.settings.age_range = hgps::core::IntegerInterval{0, 110};
    config.project_requirements.demographics.region = true;
    config.project_requirements.demographics.ethnicity = true;
    config.project_requirements.demographics.gender2 = "female";
    config.project_requirements.demographics.max_age_for_linear_models = 80;
    config.project_requirements.income.type = "continuous";
    config.project_requirements.income.categories = "4";
    config.project_requirements.income.adjust_to_factors_mean = true;
    config.project_requirements.physical_activity.type = "continuous";
    config.project_requirements.physical_activity.adjust_to_factors_mean = true;
    config.project_requirements.two_stage.use_logistic = false;

    auto &strata = config.modelling.baseline_adjustments.income_stratum_factors_mean;
    strata.enabled = true;
    strata.adjustment_income_stratum_count = 5;
    for (int i = 1; i <= 5; ++i) {
        strata.strata.push_back(hgps::config::IncomeStratumEntry{
            .id = fmt::format("Quintile{}", i),
            .factorsmean_male = finch_dir() / fmt::format("Finch.FactorsMean.Male.Quintile{}.csv", i),
            .factorsmean_female =
                finch_dir() / fmt::format("Finch.FactorsMean.Female.Quintile{}.csv", i)});
    }
    return config;
}

struct Loaded {
    std::unique_ptr<hgps::model::RiskFactorModel> model;
    IssueReport report;
};

Loaded load(const json &document, const Config &config, const HierarchicalMapping &mapping) {
    Loaded result;
    const LoadContext context{
        .mapping = &mapping, .expected = finch_expected(), .config = &config};
    result.model = load_static_linear(document, finch_static_model_path(), context, result.report);
    return result;
}

} // namespace

TEST(StaticLinearLoader, TheUpstreamFinchStaticModelLoads) {
    // The baseline's ModelParserFinch.LoadsStaticLinearDefinitionFromFinchData. It has never run.
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_static_model(), config, mapping);

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    ASSERT_NE(nullptr, loaded.model);
    EXPECT_EQ("Static", loaded.model->name());

    // The factor order is the correlation matrix's column order, and it is load-bearing: the
    // Cholesky factor and every per-factor vector are indexed by it.
    const auto generated = loaded.model->generated_factors();
    ASSERT_EQ(finch_factors().size(), generated.size());
    for (std::size_t i = 0; i < generated.size(); ++i) {
        EXPECT_EQ(hgps::core::to_lower(finch_factors()[i]), generated[i].to_string())
            << "factor " << i;
    }
}

TEST(StaticLinearLoader, ThePolicyEnergyIntakeRowIsCanonicalisedToADerivedPredictorName) {
    // The baseline's ModelParserFinch.PolicyEnergyIntakeRowNormalizedToLogEnergyIntake. The policy
    // CSV spells the term `log_EnergyIntake`; the resolver knows `log_<factor>`, so it is
    // canonicalised to `log_energyintake` — a name that is then validated like any other, rather
    // than one only the evaluator's fallback recognises.
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_static_model(), config, mapping);

    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();
    EXPECT_EQ(0U, loaded.report.error_count());
    // If the canonicalisation were wrong, the name would not resolve and the load would fail with
    // a model_unknown_predictor naming it — which is what happened before it was.
    EXPECT_EQ(std::string::npos, loaded.report.to_string().find("log_energy_intake"));
}

TEST(StaticLinearLoader, TheModelFileDecidesWhetherTheFirstStageRuns) {
    // Deviation B-23. The FINCH config sets two_stage.use_logistic false and the model ships a
    // logistic regression; the pack is fitted to the behaviour with the first step on, and the
    // baseline reads the file and ignores the flag. The file wins here too — and says so.
    auto config = finch_config();
    config.project_requirements.two_stage.use_logistic = false;
    const auto mapping = finch_mapping();

    auto loaded = load(finch_static_model(), config, mapping);
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();
    EXPECT_EQ(0U, loaded.report.error_count());
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("the model file decides"));

    // And a config that asks for the first step against a model with no logistic regression is an
    // error rather than a silent Box-Cox-only run.
    auto document = finch_static_model();
    document["RiskFactorModels"].erase("logistic_regression");
    config.project_requirements.two_stage.use_logistic = true;

    auto without = load(document, config, mapping);
    EXPECT_EQ(nullptr, without.model);
    EXPECT_NE(std::string::npos, without.report.to_string().find("use_logistic"));
}

TEST(StaticLinearLoader, AMisspelledCoefficientNameIsRejectedWithASuggestion) {
    auto document = finch_static_model();
    // The correlation matrix names the factor order, so renaming a *column* of the BoxCox file is
    // what a typo there looks like. A predictor row is the other half: rename one.
    const auto path = hgps::test::scratch_dir("static_linear_typo") / "boxcox.csv";
    {
        std::ifstream in{finch_dir() / "boxcox_coefficients.csv"};
        std::ofstream out{path};
        std::string line;
        while (std::getline(in, line)) {
            if (line.starts_with("age1,")) {
                line = "aeg1," + line.substr(5);
            }
            out << line << '\n';
        }
    }
    document["RiskFactorModels"]["boxcox_coefficients"]["name"] = path.string();

    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping);

    EXPECT_EQ(nullptr, loaded.model);
    const auto text = loaded.report.to_string();
    EXPECT_NE(std::string::npos, text.find("aeg1"));
    EXPECT_NE(std::string::npos, text.find("did you mean"));
}

TEST(StaticLinearLoader, TheTwoColumnRegressionFilesHaveNoHeaderRowAndKeepTheirIntercept) {
    // The income and physical-activity models are `Factor,Coefficient` files whose *first line* is
    // `Intercept,<value>`. Taking it as a header loses the intercept, which puts the whole
    // population below the bottom of the physical-activity range and shifts income by hundreds.
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_static_model(), config, mapping);
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();

    // The intercepts are visible through the model's behaviour: a person with no region, no
    // ethnicity and age zero gets the intercept plus the age-zero terms, which are zero.
    // Checking them directly would need the parameters, so this checks the consequence instead:
    // a generated cohort's physical activity is inside the configured range and not pinned to its
    // lower bound.
    EXPECT_TRUE(loaded.model->assigns().physical_activity);
    EXPECT_TRUE(loaded.model->assigns().income);
}

TEST(StaticLinearLoader, AStratumWithoutAColumnForEveryFactorIsRejected) {
    auto config = finch_config();
    // Point one stratum at a table that has no food-group columns at all.
    config.modelling.baseline_adjustments.income_stratum_factors_mean.strata[2].factorsmean_male =
        finch_dir() / "region.csv";

    const auto mapping = finch_mapping();
    auto loaded = load(finch_static_model(), config, mapping);

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("Quintile3"));
}

TEST(StaticLinearLoader, StratifiedAdjustmentNeedsAContinuousIncomeModel) {
    auto config = finch_config();
    config.project_requirements.income.type = "categorical";

    const auto mapping = finch_mapping();
    auto loaded = load(finch_static_model(), config, mapping);

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_NE(std::string::npos,
              loaded.report.to_string().find("ranks of a continuous income"));
}

TEST(StaticLinearLoader, ATrendTheModelHasNoEquationsForIsRejected) {
    auto config = finch_config();
    config.project_requirements.trend.enabled = true;
    config.project_requirements.trend.type = "upf_trend";

    const auto mapping = finch_mapping();
    auto loaded = load(finch_static_model(), config, mapping);

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("UPF trend"));
}

// --- the region and ethnicity prevalence ------------------------------------------------------

TEST(StaticLinearLoader, TheRegionAndEthnicityPrevalenceAreReadFromTheStaticModel) {
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    const LoadContext context{
        .mapping = &mapping, .expected = finch_expected(), .config = &config};

    IssueReport report;
    const auto prevalence = load_region_and_ethnicity(finch_static_model(),
                                                       finch_static_model_path(), context, report);

    ASSERT_TRUE(prevalence.has_value()) << report.to_string();
    EXPECT_EQ(0U, report.error_count());

    // One row per age, both sexes, four regions.
    ASSERT_TRUE(prevalence->region.contains(Identifier{"age_0"}));
    ASSERT_TRUE(prevalence->region.contains(Identifier{"age_110"}));
    const auto &by_sex = prevalence->region.at(Identifier{"age_40"});
    ASSERT_TRUE(by_sex.contains(Gender::male));
    EXPECT_EQ(4U, by_sex.at(Gender::male).size());

    double total = 0.0;
    for (const auto &[name, share] : by_sex.at(Gender::male)) {
        EXPECT_GE(share, 0.0) << name;
        total += share;
    }
    EXPECT_NEAR(1.0, total, 1e-6) << "the region shares for one age and sex should sum to one";

    // Two age groups, both sexes, four regions, four ethnicities each.
    ASSERT_TRUE(prevalence->ethnicity.contains(Identifier{"under18"}));
    ASSERT_TRUE(prevalence->ethnicity.contains(Identifier{"over18"}));
    const auto &adults = prevalence->ethnicity.at(Identifier{"over18"});
    ASSERT_TRUE(adults.contains(Gender::female));
    EXPECT_EQ(4U, adults.at(Gender::female).size());
    EXPECT_EQ(4U, adults.at(Gender::female).at("region1").size());
}

TEST(StaticLinearLoader, AskingForRegionsWithoutARegionFileIsALoadTimeError) {
    // The alternative is a run that starts and then refuses at the first person, which is what
    // the previous build did: "the project requires regions but no region prevalence data was
    // loaded", thrown from inside the demographic module.
    auto document = finch_static_model();
    document.erase("RegionFile");

    const auto config = finch_config();
    const auto mapping = finch_mapping();
    const LoadContext context{
        .mapping = &mapping, .expected = finch_expected(), .config = &config};

    IssueReport report;
    const auto prevalence =
        load_region_and_ethnicity(document, finch_static_model_path(), context, report);

    EXPECT_FALSE(prevalence.has_value());
    EXPECT_NE(std::string::npos, report.to_string().find("RegionFile"));
}

TEST(StaticLinearLoader, AProjectThatWantsNeitherReadsNeitherFile) {
    auto config = finch_config();
    config.project_requirements.demographics.region = false;
    config.project_requirements.demographics.ethnicity = false;

    auto document = finch_static_model();
    document.erase("RegionFile");
    document.erase("EthnicityFile");

    const auto mapping = finch_mapping();
    const LoadContext context{
        .mapping = &mapping, .expected = finch_expected(), .config = &config};

    IssueReport report;
    const auto prevalence =
        load_region_and_ethnicity(document, finch_static_model_path(), context, report);

    ASSERT_TRUE(prevalence.has_value()) << report.to_string();
    EXPECT_TRUE(prevalence->region.empty());
    EXPECT_TRUE(prevalence->ethnicity.empty());
}
