// The Kevin Hall model's loader, against the real converted FINCH example.
//
// These are the ports of the baseline's `KevinHallHeight` (22 tests), `KevinHallWeightQuantiles`
// (7), `KevinHallWeightValidation` (1) and `ModelParserFinch` (3) suites — **33 of the baseline's
// 35 skipped tests**. In the baseline every one of them begins
//
//     if (!std::filesystem::exists(finch / "dynamic_model.json")) { GTEST_SKIP() << …; }
//
// against a `__FILE__`-relative path, `hgps_main/input-data/data/KevinHall_FINCH`, that exists in
// neither upstream data repository (audit B-11). They have therefore never run anywhere. Here the
// path comes from the build, points at the real upstream example, and a missing fixture is a
// failure with the path in the message rather than a skip
// (docs/decisions/0006-validation-strategy.md).
#include "config/models/model_loader.h"

#include "core/interval.h"
#include "diagnostics/issue_report.h"
#include "io/json.h"
#include "model/mapping.h"
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
using hgps::config::models::detail::load_kevin_hall;
using hgps::diag::IssueReport;
using hgps::model::HierarchicalMapping;
using hgps::model::MappingEntry;
using hgps::model::SexAgeFactorTable;
using json = nlohmann::json;

std::filesystem::path finch_dir() {
    return hgps::test::upstream_examples_dir() / "KevinHall_FINCH";
}

/// The upstream FINCH dynamic model, parsed. A missing file fails rather than skips.
json finch_dynamic_model() {
    const auto path = finch_dir() / "dynamic_model.json";
    std::ifstream stream{path};
    EXPECT_TRUE(stream) << "the upstream FINCH example is not at " << path.string()
                        << "; these tests need it and must not skip (audit B-11)";
    return json::parse(stream);
}

/// The 21 FINCH food groups, which the static model generates and the dynamic model consumes.
const std::vector<std::string> &finch_foods() {
    static const std::vector<std::string> foods{
        "FoodCarbohydrate", "FoodProtein",     "FoodFat",
        "FoodSodium",       "FoodAlcohol",     "FoodLegume",
        "FoodVegetable",    "FoodFruit",       "FoodProcessedMeat",
        "FoodRedMeat",      "FoodFibre",       "FoodTotalSugar",
        "FoodAddedSugar",   "FoodSaturatedFat", "FoodPolyunsaturatedFattyAcid",
        "FoodMonounsaturatedFat", "FoodIron",  "FoodCalcium",
        "FoodVitaminC",     "FoodCopper",      "FoodZinc"};
    return foods;
}

/// A mapping with the factors the Kevin Hall model reads, and Weight given a range wide enough
/// that a newborn's weight is inside it.
HierarchicalMapping finch_mapping() {
    std::vector<MappingEntry> entries{
        MappingEntry{"Age", 0},
        MappingEntry{"Gender", 0},
        MappingEntry{"Weight", 3,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{1.0, 210.0}}},
        MappingEntry{"Height", 3,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{44.0, 203.2}}},
        MappingEntry{"BMI", 3,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{14.0, 46.0}}},
        MappingEntry{"EnergyIntake", 2,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{0.3, 19072.8}}},
        MappingEntry{"PhysicalActivity", 2,
                     hgps::model::OptionalInterval{hgps::core::DoubleInterval{1.4, 2.5}}},
    };
    return HierarchicalMapping{std::move(entries)};
}

/// An expected-value table with a column for every factor the loader checks, so that a test about
/// the height CSV is not also a test of the FactorsMean tables.
std::shared_ptr<SexAgeFactorTable> finch_expected() {
    auto table = std::make_shared<SexAgeFactorTable>();
    std::vector<std::string> names{"energyintake", "weight", "height", "physicalactivity"};
    for (const auto &food : finch_foods()) {
        names.push_back(food);
    }

    for (const auto sex : {hgps::core::Gender::male, hgps::core::Gender::female}) {
        for (const auto &name : names) {
            table->emplace(sex, hgps::core::Identifier{name}, std::vector<double>(111, 50.0));
        }
    }
    return table;
}

/// A config carrying only what the Kevin Hall loader reads.
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

struct Loaded {
    std::unique_ptr<hgps::model::RiskFactorModel> model;
    IssueReport report;
};

/// Loads the model. The document is passed by value so a test can change it first.
Loaded load(const json &document, const Config &config, const HierarchicalMapping &mapping,
            const std::shared_ptr<SexAgeFactorTable> &expected) {
    Loaded result;
    const LoadContext context{
        .mapping = &mapping, .expected = expected, .config = &config, .trend = nullptr,
        .trend_steps = nullptr,
        // The static model generates the food groups, so the dynamic model may name them.
        .extra_factors = [] {
            std::vector<hgps::core::Identifier> names;
            for (const auto &food : finch_foods()) {
                names.emplace_back(food);
            }
            return names;
        }()};

    result.model = load_kevin_hall(document, finch_dir() / "dynamic_model.json", context,
                                   result.report);
    return result;
}

/// Writes a height CSV into the scratch directory and returns its absolute path.
std::string write_csv(const std::string &name, const std::string &content) {
    const auto directory = hgps::test::scratch_dir("kevin_hall_loader_csv_" + name);
    const auto path = directory / (name + ".csv");
    std::ofstream out{path};
    out << content;
    out.close();
    return path.string();
}

void set_height_files(json &document, const std::string &female, const std::string &male) {
    document["Height"]["Female"]["name"] = female;
    document["Height"]["Male"]["name"] = male;
}

/// The legacy single-curve weight quantile shape, for a test that is not about quintile files.
void use_single_weight_quantile_files(json &document) {
    for (const auto &[sex, file] : std::vector<std::pair<std::string, std::string>>{
             {"Female", "weight_quantiles_NCDRisk_female.csv"},
             {"Male", "weight_quantiles_NCDRisk_male.csv"}}) {
        document["WeightQuantiles"][sex] = json{{"name", file},
                                                {"format", "csv"},
                                                {"delimiter", ","},
                                                {"encoding", "ASCII"},
                                                {"columns", {{"quantile", "double"}}}};
    }
}

} // namespace

// --- the example itself ---------------------------------------------------------------------

TEST(KevinHallLoader, TheUpstreamFinchDynamicModelLoads) {
    // The baseline's ModelParserFinch.LoadsStaticLinearDefinitionFromFinchData, for the dynamic
    // half. It has never run.
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, finch_expected());

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    ASSERT_NE(nullptr, loaded.model);
    EXPECT_EQ("Dynamic", loaded.model->name());
}

TEST(KevinHallLoader, TheKevinHallModelDoesNotConsultTheActiveScenario) {
    // Nothing in this model family calls `Scenario::apply`, which is why a config selecting an
    // intervention with impacts on it is refused at load time (ADR 0035, deviation B-25). The answer
    // lives on the model rather than in the loader so that it is the code's own statement about
    // itself; this is the test that the statement is true of this family.
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, finch_expected());
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();
    EXPECT_FALSE(loaded.model->applies_the_active_scenario());
}

TEST(KevinHallLoader, TheQuintileWeightCurvesAreLoadedAndDifferFromEachOther) {
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, finch_expected());
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    ASSERT_NE(nullptr, model);

    hgps::model::Person poor;
    poor.gender = hgps::core::Gender::male;
    poor.has_income_adjustment_stratum = true;
    poor.income_adjustment_stratum = 0;

    hgps::model::Person rich = poor;
    rich.income_adjustment_stratum = 4;

    const auto &lowest = model->weight_quantiles_for(poor);
    const auto &highest = model->weight_quantiles_for(rich);

    ASSERT_FALSE(lowest.empty());
    ASSERT_FALSE(highest.empty());
    EXPECT_NE(lowest, highest) << "the five quintile files are not five different curves";

    // Sorted, because the quantile lookup is a binary search over them.
    for (const auto *curve : {&lowest, &highest}) {
        for (std::size_t i = 1; i < curve->size(); ++i) {
            EXPECT_LE((*curve)[i - 1], (*curve)[i]);
        }
    }
}

TEST(KevinHallLoader, AQuantileFileIsReadFromTheColumnItsBlockNames) {
    // These files are R `write.csv` output: an unnamed row-index column, then `quantile`. Reading
    // column 0 gives 1, 2, 3, … and the weight curve becomes the row numbers — which produces a
    // population in which everybody of an age and sex weighs exactly the same.
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, finch_expected());
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    ASSERT_NE(nullptr, model);

    hgps::model::Person person;
    person.gender = hgps::core::Gender::male;
    const auto &curve = model->weight_quantiles_for(person);

    ASSERT_FALSE(curve.empty());
    // The quantiles are weight multipliers around 1, not row indices.
    EXPECT_GT(curve.front(), 0.1);
    EXPECT_LT(curve.back(), 10.0);
}

TEST(KevinHallLoader, TheHeightParametersAreOneRowPerIncomeQuintile) {
    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, finch_expected());
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    ASSERT_NE(nullptr, model);

    hgps::model::Person person;
    person.gender = hgps::core::Gender::male;
    person.has_income_adjustment_stratum = true;

    std::vector<double> slopes;
    for (std::size_t stratum = 0; stratum < 5; ++stratum) {
        person.income_adjustment_stratum = stratum;
        slopes.push_back(model->height_params_for(person).slope);
        EXPECT_GT(model->height_params_for(person).stddev, 0.0);
    }

    EXPECT_NE(slopes.front(), slopes.back())
        << "the five quintile rows of height_male.csv are not five different slopes";
}

// --- the height CSV, in each of its shapes --------------------------------------------------

TEST(KevinHallHeightCsv, TheLegacyScalarHeightBlockStillLoads) {
    auto document = finch_dynamic_model();
    use_single_weight_quantile_files(document);
    document.erase("Height");
    document["HeightSlope"] = json{{"Female", 0.123}, {"Male", 0.127}};
    document["HeightStdDev"] = json{{"Female", 0.041}, {"Male", 0.043}};

    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    ASSERT_NE(nullptr, loaded.model);

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    ASSERT_NE(nullptr, model);
    hgps::model::Person person;
    person.gender = hgps::core::Gender::male;
    EXPECT_DOUBLE_EQ(0.127, model->height_params_for(person).slope);
}

TEST(KevinHallHeightCsv, OneRowBroadcastsToEveryStratum) {
    auto document = finch_dynamic_model();
    set_height_files(document, write_csv("female_single", "slope,std\n0.18,0.05\n"),
                     write_csv("male_single", "slope,std\n0.20,0.06\n"));

    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    hgps::model::Person person;
    person.gender = hgps::core::Gender::male;
    person.has_income_adjustment_stratum = true;
    for (std::size_t stratum = 0; stratum < 5; ++stratum) {
        person.income_adjustment_stratum = stratum;
        EXPECT_DOUBLE_EQ(0.20, model->height_params_for(person).slope);
    }
}

TEST(KevinHallHeightCsv, AsManyRowsAsStrataLoads) {
    const std::string rows =
        ",slope,std\nq1,0.11,0.041\nq2,0.12,0.042\nq3,0.13,0.043\nq4,0.14,0.044\nq5,0.15,0.045\n";
    auto document = finch_dynamic_model();
    set_height_files(document, write_csv("female_multi", rows), write_csv("male_multi", rows));

    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    hgps::model::Person person;
    person.gender = hgps::core::Gender::female;
    person.has_income_adjustment_stratum = true;
    person.income_adjustment_stratum = 2;
    EXPECT_DOUBLE_EQ(0.13, model->height_params_for(person).slope);
}

TEST(KevinHallHeightCsv, TheWrongNumberOfRowsForTheStrataIsAnError) {
    const std::string rows = "slope,std\n0.11,0.041\n0.12,0.042\n0.13,0.043\n";
    auto document = finch_dynamic_model();
    set_height_files(document, write_csv("female_invalid", rows), write_csv("male_invalid", rows));

    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_GT(loaded.report.error_count(), 0U);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("3 rows"));
}

TEST(KevinHallHeightCsv, AThreeColumnFileWithALabelColumnLoads) {
    const std::string rows = ",slope,std\nincome_quintile1,0.11,0.041\n"
                             "income_quintile2,0.12,0.042\nincome_quintile3,0.13,0.043\n"
                             "income_quintile4,0.14,0.044\nincome_quintile5,0.15,0.045\n";
    auto document = finch_dynamic_model();
    set_height_files(document, write_csv("female_keyed", rows), write_csv("male_keyed", rows));

    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    EXPECT_NE(nullptr, loaded.model);
}

TEST(KevinHallHeightCsv, FourColumnsIsAnError) {
    const std::string rows = "key,slope,std,extra\nq1,0.1,0.2,0.3\n";
    auto document = finch_dynamic_model();
    set_height_files(document, write_csv("female_badcols", rows), write_csv("male_badcols", rows));

    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_GT(loaded.report.error_count(), 0U);
}

TEST(KevinHallHeightCsv, OneColumnIsAnError) {
    auto document = finch_dynamic_model();
    set_height_files(document, write_csv("female_one_col", "0.18\n"),
                     write_csv("male_one_col", "0.18\n"));

    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_GT(loaded.report.error_count(), 0U);
}

TEST(KevinHallHeightCsv, TheTwoSexesMayUseDifferentShapes) {
    auto document = finch_dynamic_model();
    set_height_files(
        document, write_csv("female_broadcast", "slope,std\n0.17,0.051\n"),
        write_csv("male_strata",
                  "slope,std\n0.20,0.041\n0.21,0.042\n0.22,0.043\n0.23,0.044\n0.24,0.045\n"));

    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    hgps::model::Person female;
    female.gender = hgps::core::Gender::female;
    female.has_income_adjustment_stratum = true;
    female.income_adjustment_stratum = 3;
    EXPECT_DOUBLE_EQ(0.17, model->height_params_for(female).slope);

    hgps::model::Person male = female;
    male.gender = hgps::core::Gender::male;
    EXPECT_DOUBLE_EQ(0.23, model->height_params_for(male).slope);
}

TEST(KevinHallHeightCsv, ManyRowsWithNoStrataUsesTheFirstAndSaysSo) {
    const std::string rows = "slope,std\n0.11,0.041\n0.12,0.042\n";
    auto document = finch_dynamic_model();
    use_single_weight_quantile_files(document);
    set_height_files(document, write_csv("female_norows", rows), write_csv("male_norows", rows));

    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();
    EXPECT_EQ(0U, loaded.report.error_count());
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("only the first is used"));

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    hgps::model::Person person;
    person.gender = hgps::core::Gender::male;
    EXPECT_DOUBLE_EQ(0.11, model->height_params_for(person).slope);
}

TEST(KevinHallHeightCsv, AHeaderWithNoRowsIsAnError) {
    auto document = finch_dynamic_model();
    set_height_files(document, write_csv("female_header_only", "slope,std\n"),
                     write_csv("male_header_only", "slope,std\n"));

    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_GT(loaded.report.error_count(), 0U);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("no data rows"));
}

TEST(KevinHallHeightCsv, OneRowLoadsWhenThereIsOneStratum) {
    auto document = finch_dynamic_model();
    use_single_weight_quantile_files(document);
    set_height_files(document, write_csv("female_one_stratum", "slope,std\n0.18,0.05\n"),
                     write_csv("male_one_stratum", "slope,std\n0.20,0.06\n"));

    const auto config = finch_config(false, 1);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    EXPECT_NE(nullptr, loaded.model);
}

TEST(KevinHallHeightCsv, ASemicolonDelimiterIsHonoured) {
    auto document = finch_dynamic_model();
    use_single_weight_quantile_files(document);
    set_height_files(document, write_csv("female_semicolon", "slope;std\n0.18;0.05\n"),
                     write_csv("male_semicolon", "slope;std\n0.20;0.06\n"));
    document["Height"]["Female"]["delimiter"] = ";";
    document["Height"]["Male"]["delimiter"] = ";";

    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    ASSERT_NE(nullptr, loaded.model);

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    hgps::model::Person person;
    person.gender = hgps::core::Gender::female;
    EXPECT_DOUBLE_EQ(0.18, model->height_params_for(person).slope);
}

TEST(KevinHallHeightCsv, AFileWithNoHeaderRowStillLoads) {
    auto document = finch_dynamic_model();
    set_height_files(document, write_csv("female_no_header", "0.18,0.05\n"),
                     write_csv("male_no_header", "0.20,0.06\n"));

    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    ASSERT_NE(nullptr, loaded.model);

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    hgps::model::Person person;
    person.gender = hgps::core::Gender::male;
    person.has_income_adjustment_stratum = true;
    person.income_adjustment_stratum = 2;
    EXPECT_DOUBLE_EQ(0.20, model->height_params_for(person).slope);
}

TEST(KevinHallHeightCsv, AZeroResidualStandardDeviationIsAnError) {
    // Not one of the baseline's: with zero, every person of an age and sex is exactly the expected
    // height, which is a silently degenerate model rather than a modelling choice.
    auto document = finch_dynamic_model();
    use_single_weight_quantile_files(document);
    set_height_files(document, write_csv("female_zero_sd", "slope,std\n0.18,0\n"),
                     write_csv("male_zero_sd", "slope,std\n0.20,0\n"));

    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_GT(loaded.report.error_count(), 0U);
}

// --- the weight quantile files ---------------------------------------------------------------

TEST(KevinHallWeightQuantileFiles, TheLegacySingleFileShapeStillLoads) {
    auto document = finch_dynamic_model();
    use_single_weight_quantile_files(document);

    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    EXPECT_NE(nullptr, loaded.model);
}

TEST(KevinHallWeightQuantileFiles, OneFileBroadcastsToEveryStratum) {
    auto document = finch_dynamic_model();
    use_single_weight_quantile_files(document);

    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());
    ASSERT_NE(nullptr, loaded.model) << loaded.report.to_string();

    auto *model = dynamic_cast<hgps::model::KevinHallModel *>(loaded.model.get());
    hgps::model::Person person;
    person.gender = hgps::core::Gender::male;
    person.has_income_adjustment_stratum = true;
    person.income_adjustment_stratum = 0;
    const auto first = model->weight_quantiles_for(person);
    person.income_adjustment_stratum = 4;
    EXPECT_EQ(first, model->weight_quantiles_for(person));
}

TEST(KevinHallWeightQuantileFiles, AsManyQuintileFilesAsStrataLoads) {
    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, finch_expected());

    EXPECT_EQ(0U, loaded.report.error_count()) << loaded.report.to_string();
    EXPECT_NE(nullptr, loaded.model);
}

TEST(KevinHallWeightQuantileFiles, TheWrongNumberOfQuintileFilesIsAnError) {
    const auto config = finch_config(true, 3);
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_GT(loaded.report.error_count(), 0U);
    EXPECT_NE(std::string::npos,
              loaded.report.to_string().find("adjustment_income_stratum_count"));
}

TEST(KevinHallWeightQuantileFiles, QuintileFilesNeedTheStratumAdjustmentSwitchedOn) {
    const auto config = finch_config(false, 0);
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_GT(loaded.report.error_count(), 0U);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("income_stratum_factors_mean"));
}

TEST(KevinHallWeightQuantileFiles, AnUnsupportedQuintileKeyIsAnError) {
    auto document = finch_dynamic_model();
    document["WeightQuantiles"]["Male"]["Decile1"] =
        document["WeightQuantiles"]["Male"]["Quintile1"];
    document["WeightQuantiles"]["Male"].erase("Quintile1");

    const auto config = finch_config(true, 5);
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("Quintile1..N"));
}

// --- the FactorsMean columns the model needs --------------------------------------------------

TEST(KevinHallLoader, AMissingFoodColumnInTheFactorsMeanTablesIsALoadTimeError) {
    // The derived expected values read every food group's expected intake, so a table without one
    // of them produces a model that cannot compute a nutrient. The baseline discovers this at the
    // first person of the first year, inside a parallel loop.
    auto expected = finch_expected();
    auto trimmed = std::make_shared<SexAgeFactorTable>();
    for (const auto &[sex, by_factor] : *expected) {
        for (const auto &[factor, values] : by_factor) {
            if (factor.to_string() == "foodzinc") {
                continue;
            }
            trimmed->emplace(sex, factor, values);
        }
    }

    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(finch_dynamic_model(), config, mapping, trimmed);

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("foodzinc"));
}

TEST(KevinHallLoader, AFoodGroupNoModelGeneratesIsALoadTimeError) {
    auto document = finch_dynamic_model();
    document["Foods"][0]["Name"] = "FoodUnobtainium";

    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("FoodUnobtainium"));
}

TEST(KevinHallLoader, ANutrientNoFoodDeclaresIsALoadTimeError) {
    auto document = finch_dynamic_model();
    document["Foods"][0]["Nutrients"]["Unobtainium"] = 1.0;

    const auto config = finch_config();
    const auto mapping = finch_mapping();
    auto loaded = load(document, config, mapping, finch_expected());

    EXPECT_EQ(nullptr, loaded.model);
    EXPECT_NE(std::string::npos, loaded.report.to_string().find("Unobtainium"));
}
