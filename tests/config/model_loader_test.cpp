// The risk-factor model loaders, against the member names the upstream fitted-model files
// actually use.
//
// There was no test here when the loaders were written, and the loaders read the member names of
// the *internal* structures rather than the ones in the files: `transition` for `m`,
// `residual_distribution` for `s`, `residuals_standard_deviation` for
// `residualsStandardDeviation`. The synthetic fixture was generated to match, so everything
// passed until the real HLM_France model was loaded and produced 54 located errors at once. The
// literals below are the file format, taken from `hgps_main_examples/HLM_France/static_model.json`
// and `dynamic_model.json`, and they are the point of this file.
#include "config/models/model_loader.h"

#include "diagnostics/issue_report.h"
#include "model/mapping.h"
#include "model/riskfactor/risk_factor_model.h"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::config::Config;
using hgps::config::models::LoadContext;
using hgps::config::models::detail::load_ebhlm;
using hgps::config::models::detail::load_hlm;
using hgps::diag::IssueCode;
using hgps::diag::IssueReport;
using hgps::model::HierarchicalMapping;
using hgps::model::MappingEntry;
using json = nlohmann::json;

/// Gender, Age, SES at level 0; Sodium at level 1; BMI at level 2 — the shape of the France
/// model, cut down to two modelled factors.
HierarchicalMapping test_mapping() {
    return HierarchicalMapping{std::vector<MappingEntry>{
        MappingEntry{"Gender", 0},
        MappingEntry{"Age", 0},
        MappingEntry{"SES", 0},
        MappingEntry{"Sodium", 1, hgps::model::OptionalInterval{hgps::core::DoubleInterval{1.0, 9.0}}},
        MappingEntry{"BMI", 2, hgps::model::OptionalInterval{hgps::core::DoubleInterval{13.0, 40.0}}},
    }};
}

/// The FactorsMean tables, which any adjusting model needs before it will build.
std::shared_ptr<hgps::model::SexAgeFactorTable> test_expected_values() {
    auto table = std::make_shared<hgps::model::SexAgeFactorTable>();
    for (const auto sex : {hgps::core::Gender::male, hgps::core::Gender::female}) {
        table->emplace(sex, hgps::core::Identifier{"Sodium"}, std::vector<double>(101, 3.5));
        table->emplace(sex, hgps::core::Identifier{"BMI"}, std::vector<double>(101, 25.0));
    }
    return table;
}

struct Fixture {
    HierarchicalMapping mapping{test_mapping()};
    Config config{};
    std::shared_ptr<const hgps::model::SexAgeFactorTable> expected{test_expected_values()};

    LoadContext context() const {
        return LoadContext{.mapping = &mapping, .expected = expected, .config = &config};
    }
};

json coefficient(double value) {
    return json{{"value", value}, {"stdError", 0.1}, {"tValue", 2.0}, {"pValue", 0.01}};
}

json matrix(std::vector<double> data, std::size_t rows) {
    return json{{"rows", rows}, {"cols", data.size() / rows}, {"data", data}};
}

/// A minimal but complete `HLM` static model, in the upstream file format.
json static_model() {
    json document;
    document["$schema"] = "schemas/v1/static_model.json";
    document["ModelName"] = "HLM";

    document["models"]["Sodium"] = json{
        {"formula", "result = lm(Sodium ~ Gender + Age + SES)"},
        {"coefficients", json{{"Intercept", coefficient(3.5)},
                              {"Gender", coefficient(0.2)},
                              {"Age", coefficient(0.01)},
                              {"SES", coefficient(0.05)}}},
        // The two big per-observation arrays the files carry and the simulation does not read.
        {"residuals", json::array({0.1, -0.1, 0.2})},
        {"fittedValues", json::array({3.4, 3.6, 3.5})},
        {"residualsStandardDeviation", 0.5},
        {"rSquared", 0.31}};

    document["models"]["BMI"] =
        json{{"formula", "result = lm(BMI ~ Gender + Age + Sodium)"},
             {"coefficients", json{{"Intercept", coefficient(20.0)},
                                   {"Gender", coefficient(0.8)},
                                   {"Age", coefficient(0.05)},
                                   {"Sodium", coefficient(0.4)}}},
             {"residuals", json::array({1.0, -1.0})},
             {"fittedValues", json::array({25.0, 24.0})},
             {"residualsStandardDeviation", 2.2},
             {"rSquared", 0.35}};

    document["levels"]["1"] = json{{"variables", json::array({"Sodium"})},
                                   {"m", matrix({1.0}, 1)},
                                   {"w", matrix({1.0}, 1)},
                                   {"s", matrix({-0.4, 0.0, 0.4}, 3)},
                                   {"correlation", matrix({1.0}, 1)},
                                   {"variances", json::array({1.0})}};

    document["levels"]["2"] = json{{"variables", json::array({"BMI"})},
                                   {"m", matrix({1.0}, 1)},
                                   {"w", matrix({1.0}, 1)},
                                   {"s", matrix({-2.0, 0.0, 2.0}, 3)},
                                   {"correlation", matrix({1.0}, 1)},
                                   {"variances", json::array({1.0})}};

    return document;
}

/// A minimal but complete `EBHLM` dynamic model, in the upstream file format.
json dynamic_model() {
    json document;
    document["ModelName"] = "EBHLM";
    document["Country"] = json{{"Code", 250}, {"Name", "France"}, {"Alpha2", "FR"},
                               {"Alpha3", "FRA"}};
    document["BoundaryPercentage"] = 0.05;
    document["Variables"] = json::array({json{{"Name", "dSodium"}, {"Factor", "Sodium"}},
                                         json{{"Name", "dBMI"}, {"Factor", "BMI"}}});

    const auto equations = [](double intercept) {
        return json::array({
            json{{"Name", "Sodium"},
                 {"Coefficients", json{{"Intercept", intercept}, {"Age", 0.001}, {"SES", 0.002}}},
                 {"ResidualsStandardDeviation", 0.3}},
            json{{"Name", "BMI"},
                 {"Coefficients",
                  json{{"Intercept", intercept}, {"Age", 0.01}, {"dSodium", 0.5}}},
                 {"ResidualsStandardDeviation", 0.34}},
        });
    };

    document["Equations"]["0-19"] = json{{"Male", equations(0.1)}, {"Female", equations(0.2)}};
    document["Equations"]["20-100"] = json{{"Male", equations(0.3)}, {"Female", equations(0.4)}};
    return document;
}

} // namespace

TEST(ModelLoader, ReadsTheStaticModelInTheFormatTheFilesUse) {
    Fixture fixture;
    IssueReport report;

    const auto model = load_hlm(static_model(), "static_model.json", fixture.context(), report);

    ASSERT_NE(nullptr, model) << report.to_string();
    EXPECT_FALSE(report.has_errors()) << report.to_string();
    EXPECT_EQ(hgps::model::RiskFactorModelType::Static, model->type());
}

TEST(ModelLoader, ReadsTheDynamicModelInTheFormatTheFilesUse) {
    Fixture fixture;
    IssueReport report;

    const auto model = load_ebhlm(dynamic_model(), "dynamic_model.json", fixture.context(), report);

    ASSERT_NE(nullptr, model) << report.to_string();
    EXPECT_FALSE(report.has_errors()) << report.to_string();
    EXPECT_EQ(hgps::model::RiskFactorModelType::Dynamic, model->type());
}

TEST(ModelLoader, OnlyTheDynamicHierarchicalModelConsultsTheActiveScenario) {
    // `Scenario::apply` has one call site in this build, in the dynamic HLM's `update_exposure`, and
    // one in the baseline, in the same model. Every model family answers for itself whether it makes
    // that call, and the load-time intervention check reads the answer (ADR 0035, deviation D-39).
    // Here are two of the four; the other two are in the FINCH loader tests, next to their fixtures.
    Fixture fixture;
    IssueReport report;

    const auto dynamic_hlm =
        load_ebhlm(dynamic_model(), "dynamic_model.json", fixture.context(), report);
    ASSERT_NE(nullptr, dynamic_hlm) << report.to_string();
    EXPECT_TRUE(dynamic_hlm->applies_the_active_scenario());

    const auto static_hlm = load_hlm(static_model(), "static_model.json", fixture.context(), report);
    ASSERT_NE(nullptr, static_hlm) << report.to_string();
    EXPECT_FALSE(static_hlm->applies_the_active_scenario())
        << "a static model runs once, before any policy is active; it has nothing to apply";
}

TEST(ModelLoader, RejectsTheInternalNamesForTheLevelMatrices) {
    // The bug this file exists for: `m`, `w` and `s` are what the files say. Reading
    // `transition`, `inverse_transition` and `residual_distribution` instead made every real
    // model file fail, and every synthetic one pass.
    Fixture fixture;
    IssueReport report;

    auto document = static_model();
    auto &level = document["levels"]["1"];
    level["transition"] = level["m"];
    level["inverse_transition"] = level["w"];
    level["residual_distribution"] = level["s"];
    level.erase("m");
    level.erase("w");
    level.erase("s");

    EXPECT_EQ(nullptr, load_hlm(document, "static_model.json", fixture.context(), report));
    ASSERT_TRUE(report.has_errors());
    EXPECT_TRUE(report.contains(IssueCode::config_unknown_property));
    EXPECT_NE(std::string::npos, report.to_string().find("transition"));
}

TEST(ModelLoader, RejectsAnUnknownMemberWithANearMissSuggestion) {
    Fixture fixture;
    IssueReport report;

    auto document = static_model();
    document["models"]["Sodium"]["rsquared"] = document["models"]["Sodium"]["rSquared"];
    document["models"]["Sodium"].erase("rSquared");

    EXPECT_EQ(nullptr, load_hlm(document, "static_model.json", fixture.context(), report));
    EXPECT_TRUE(report.contains(IssueCode::config_unknown_property));
    EXPECT_NE(std::string::npos, report.to_string().find("did you mean 'rSquared'"));
}

TEST(ModelLoader, RejectsACoefficientNameThatIsNotADeclaredPredictor) {
    // ADR 0018: a misspelled coefficient name must stop the run at load time, not silently
    // contribute zero to every person's value for forty years.
    Fixture fixture;
    IssueReport report;

    auto document = static_model();
    document["models"]["Sodium"]["coefficients"]["Sex"] = coefficient(0.2);

    EXPECT_EQ(nullptr, load_hlm(document, "static_model.json", fixture.context(), report));
    ASSERT_TRUE(report.contains(IssueCode::model_unknown_predictor));
    const auto text = report.to_string();
    EXPECT_NE(std::string::npos, text.find("Sex"));
    EXPECT_NE(std::string::npos, text.find("/models/Sodium/coefficients/Sex"));
}

TEST(ModelLoader, AcceptsTheDerivedPredictorsThatAreNotDeclaredFactors) {
    // Intercept is a metadata row, not a factor, and so are the age powers and the boolean
    // helpers the models use. They resolve without appearing in `risk_factors`.
    Fixture fixture;
    IssueReport report;

    auto document = static_model();
    document["models"]["BMI"]["coefficients"]["over18"] = coefficient(0.3);

    EXPECT_NE(nullptr, load_hlm(document, "static_model.json", fixture.context(), report))
        << report.to_string();
    EXPECT_FALSE(report.has_errors()) << report.to_string();
}

TEST(ModelLoader, RejectsALevelWhoseMatricesDoNotMatchItsVariableCount) {
    // The baseline checks none of this and reads out of bounds when a file disagrees.
    Fixture fixture;
    IssueReport report;

    auto document = static_model();
    document["levels"]["1"]["variables"] = json::array({"Sodium", "BMI"});

    EXPECT_EQ(nullptr, load_hlm(document, "static_model.json", fixture.context(), report));
    ASSERT_TRUE(report.contains(IssueCode::model_dimension_mismatch));
    const auto text = report.to_string();
    EXPECT_NE(std::string::npos, text.find("2 variables need a 2x2 transition matrix 'm'"));
    EXPECT_NE(std::string::npos, text.find("residual columns in 's'"));
}

TEST(ModelLoader, RejectsAMatrixWhoseDataLengthDisagreesWithItsShape) {
    Fixture fixture;
    IssueReport report;

    auto document = static_model();
    document["levels"]["1"]["s"] = matrix({-0.4, 0.0, 0.4}, 3);
    document["levels"]["1"]["s"]["cols"] = 2; // says 3x2, carries 3 values

    EXPECT_EQ(nullptr, load_hlm(document, "static_model.json", fixture.context(), report));
    EXPECT_TRUE(report.contains(IssueCode::model_dimension_mismatch));
    EXPECT_NE(std::string::npos, report.to_string().find("3x2 needs 6 values, found 3"));
}

TEST(ModelLoader, RejectsAStaticModelWithNoEquationForADeclaredFactor) {
    Fixture fixture;
    IssueReport report;

    auto document = static_model();
    document["models"].erase("BMI");

    EXPECT_EQ(nullptr, load_hlm(document, "static_model.json", fixture.context(), report));
    ASSERT_TRUE(report.contains(IssueCode::model_missing_key));
    EXPECT_NE(std::string::npos, report.to_string().find("BMI"));
}

TEST(ModelLoader, RejectsADynamicModelWhoseDeltaVariableNamesNothingDeclared) {
    Fixture fixture;
    IssueReport report;

    auto document = dynamic_model();
    document["Variables"][0]["Factor"] = "Salt";

    EXPECT_EQ(nullptr, load_ebhlm(document, "dynamic_model.json", fixture.context(), report));
    ASSERT_TRUE(report.contains(IssueCode::model_unknown_predictor));
    EXPECT_NE(std::string::npos, report.to_string().find("Salt"));
}

TEST(ModelLoader, RejectsABoundaryPercentageOutsideTheUnitInterval) {
    Fixture fixture;

    for (const double bad : {0.0, 1.0, -0.1, 1.5}) {
        IssueReport report;
        auto document = dynamic_model();
        document["BoundaryPercentage"] = bad;

        EXPECT_EQ(nullptr, load_ebhlm(document, "dynamic_model.json", fixture.context(), report))
            << "BoundaryPercentage " << bad << " should be rejected";
        EXPECT_TRUE(report.contains(IssueCode::model_bad_value));
    }
}

TEST(ModelLoader, RejectsAnAgeBandThatIsNotAnInterval) {
    Fixture fixture;
    IssueReport report;

    auto document = dynamic_model();
    document["Equations"]["adults"] = document["Equations"]["20-100"];

    EXPECT_EQ(nullptr, load_ebhlm(document, "dynamic_model.json", fixture.context(), report));
    ASSERT_TRUE(report.contains(IssueCode::model_bad_value));
    EXPECT_NE(std::string::npos, report.to_string().find("'adults' is not an age band"));
}

TEST(ModelLoader, ReportsEveryProblemInOneGo) {
    // The whole point of the accumulated report (ADR 0007): a reader fixes their file once, not
    // once per run.
    Fixture fixture;
    IssueReport report;

    auto document = static_model();
    document["models"]["Sodium"]["coefficients"]["Sex"] = coefficient(0.2);
    document["models"]["BMI"]["coefficients"]["Height"] = coefficient(0.2);
    document["levels"]["2"]["variables"] = json::array({"BMI", "Sodium"});

    EXPECT_EQ(nullptr, load_hlm(document, "static_model.json", fixture.context(), report));
    EXPECT_GE(report.error_count(), 3U) << report.to_string();
    const auto text = report.to_string();
    EXPECT_NE(std::string::npos, text.find("Sex"));
    EXPECT_NE(std::string::npos, text.find("Height"));
}
