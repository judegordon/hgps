// Ports the intent of the baseline's Configuration.Test.cpp, ConfigSchemaExpanded.Test.cpp and
// ConfigLegacyFields.Test.cpp, adapted to config v2 and to accumulated diagnostics rather than a
// throw per problem. docs/test-port-map.md records the mapping suite by suite.
#include "config/loader.h"

#include "support/config_fixture.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace {

using hgps::config::Config;
using hgps::config::LoadOptions;
using hgps::diag::IssueCode;
using hgps::diag::IssueReport;
using hgps::test::ConfigFixture;

struct LoadResult {
    std::optional<Config> config;
    IssueReport report;
};

LoadResult load(const ConfigFixture &fixture, const nlohmann::json &document,
                LoadOptions options = {}) {
    LoadResult result;
    result.config =
        hgps::config::load_from_json(document, fixture.dir(), options, result.report);
    return result;
}

} // namespace

TEST(ConfigParsing, LoadsAValidDocument) {
    const ConfigFixture fixture{"config_valid"};
    auto [config, report] = load(fixture, fixture.document());

    ASSERT_TRUE(config.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors());

    EXPECT_EQ(2, config->version);
    EXPECT_EQ(123456789U, config->running.seed);
    EXPECT_EQ(2010U, config->running.start_time);
    EXPECT_EQ(2050U, config->running.stop_time);
    EXPECT_EQ(1U, config->running.trial_runs);
    EXPECT_EQ(std::vector<std::string>({"alzheimer", "asthma"}), config->running.diseases);
    EXPECT_FALSE(config->running.active_intervention.has_value());

    EXPECT_EQ("FRA", config->settings.country_code);
    EXPECT_DOUBLE_EQ(0.0001, config->settings.size_fraction);
    EXPECT_EQ(hgps::core::IntegerInterval(0, 100), config->settings.age_range);

    EXPECT_EQ(fixture.dir() / "France.DataFile.csv", config->dataset.name);
    EXPECT_EQ(3U, config->dataset.columns.size());
    EXPECT_EQ("Age", config->dataset.columns.front().name);

    EXPECT_EQ("normal", config->modelling.ses_model.function_name);
    EXPECT_EQ(3U, config->modelling.risk_factors.size());
    EXPECT_EQ(fixture.dir() / "static_model.json",
              config->modelling.risk_factor_models.at("static"));
    EXPECT_EQ(fixture.dir() / "France.FactorsMean.Male.csv",
              config->modelling.baseline_adjustments.file_names.at("factorsmean_male"));

    EXPECT_EQ(5U, config->output.comorbidities);
    EXPECT_EQ("result.csv", config->output.file_name);
}

TEST(ConfigParsing, ReportsEveryProblemInOnePass) {
    // The behaviour the two-tier diagnostics design exists for: upstream reports one config error
    // per run, so five mistakes take five runs to find.
    const ConfigFixture fixture{"config_many_errors"};
    auto document = fixture.document();
    document["running"].erase("seed");
    document["inputs"]["settings"].erase("country_code");
    document["modelling"]["ses_model"]["function_name"] = "lognormal";
    document["output"]["comorbidities"] = "five";
    document["running"]["stop_time"] = 2000;

    auto [config, report] = load(fixture, document);

    EXPECT_FALSE(config.has_value());
    EXPECT_GE(report.error_count(), 5U) << report.to_string();
    EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    EXPECT_TRUE(report.contains(IssueCode::config_wrong_type));
}

TEST(ConfigParsing, CheckVersion) {
    const ConfigFixture fixture{"config_version"};

    for (const auto version : {0, 1, 3, -1}) {
        auto document = fixture.document();
        document["version"] = version;
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value()) << "version " << version;
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        auto document = fixture.document();
        document.erase("version");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        // A string, a bool and an object are all the wrong type, not a wrong value.
        for (const auto &value : {nlohmann::json("2"), nlohmann::json(true),
                                 nlohmann::json::object(), nlohmann::json::array({2})}) {
            auto document = fixture.document();
            document["version"] = value;
            auto [config, report] = load(fixture, document);
            EXPECT_FALSE(config.has_value()) << value.dump();
            EXPECT_TRUE(report.contains(IssueCode::config_wrong_type));
        }
    }
}

TEST(ConfigParsing, AnUnseededConfigIsRejected) {
    // Required by the validation plan, and the point of determinism clause D1. Baseline finding
    // B-06: an absent seed produced a silently irreproducible run whose results file then
    // recorded the seed as 0.
    const ConfigFixture fixture{"config_unseeded"};

    {
        auto document = fixture.document();
        document["running"].erase("seed");

        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        ASSERT_TRUE(report.contains(IssueCode::config_missing_required));

        bool mentions_seed = false;
        for (const auto &issue : report.issues()) {
            mentions_seed = mentions_seed || issue.location.field == "/running/seed";
        }
        EXPECT_TRUE(mentions_seed) << report.to_string();
    }

    {
        // The v1 array form, including the empty array that meant "unseeded".
        for (const auto &value : {nlohmann::json::array(), nlohmann::json::array({42})}) {
            auto document = fixture.document();
            document["running"]["seed"] = value;

            auto [config, report] = load(fixture, document);
            EXPECT_FALSE(config.has_value()) << value.dump();
            EXPECT_TRUE(report.contains(IssueCode::config_wrong_type));
            EXPECT_NE(std::string::npos, report.to_string().find("not an array"));
        }
    }

    {
        auto document = fixture.document();
        document["running"]["seed"] = -1;
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }
}

TEST(ConfigParsing, ProjectRequirementsIsRequired) {
    // Changed from the baseline on purpose (audit D-03, ADR 0010): upstream makes this optional
    // and no primary example config has it, while most current behaviour is gated on it.
    const ConfigFixture fixture{"config_requirements_required"};
    auto document = fixture.document();
    document.erase("project_requirements");

    auto [config, report] = load(fixture, document);
    EXPECT_FALSE(config.has_value());
    ASSERT_TRUE(report.contains(IssueCode::config_missing_required));
    EXPECT_NE(std::string::npos, report.to_string().find("convert-config"));
}

TEST(ConfigParsing, ProjectRequirementsDefaultsAreAppliedAndReported) {
    const ConfigFixture fixture{"config_requirements_defaults"};
    auto document = fixture.document();
    document["project_requirements"] = nlohmann::json::parse(R"({
        "demographics": {},
        "income": {},
        "physical_activity": {},
        "risk_factors": {},
        "trend": {},
        "two_stage": {}
    })");

    auto [config, report] = load(fixture, document);
    ASSERT_TRUE(config.has_value()) << report.to_string();

    const auto &requirements = config->project_requirements;
    EXPECT_TRUE(requirements.demographics.age);
    EXPECT_TRUE(requirements.demographics.gender);
    EXPECT_FALSE(requirements.demographics.region);
    EXPECT_FALSE(requirements.demographics.ethnicity);
    EXPECT_EQ("male", requirements.demographics.gender2);
    EXPECT_FALSE(requirements.demographics.max_age_for_linear_models.has_value());

    EXPECT_TRUE(requirements.income.enabled);
    EXPECT_EQ("categorical", requirements.income.type);
    EXPECT_EQ("3", requirements.income.categories);
    EXPECT_FALSE(requirements.income.adjust_to_factors_mean);
    EXPECT_TRUE(requirements.income.income_based_csv_output);

    EXPECT_EQ("simple", requirements.physical_activity.type);
    EXPECT_TRUE(requirements.risk_factors.adjust_to_factors_mean);
    EXPECT_TRUE(requirements.risk_factors.trended);
    EXPECT_FALSE(requirements.trend.enabled);
    EXPECT_EQ("null", requirements.trend.type);
    EXPECT_FALSE(requirements.two_stage.use_logistic);

    // Not silent: every applied default is a warning that names the value used.
    EXPECT_FALSE(report.has_errors());
    EXPECT_GT(report.warning_count(), 10U);
    EXPECT_NE(std::string::npos, report.to_string().find("default"));
}

TEST(ConfigParsing, ProjectRequirementsRejectsBadValues) {
    const ConfigFixture fixture{"config_requirements_bad"};

    const auto expect_error = [&fixture](const std::string &pointer, const nlohmann::json &value) {
        auto document = fixture.document();
        document["project_requirements"][nlohmann::json::json_pointer{pointer}] = value;
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value()) << pointer << " = " << value.dump();
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value)) << report.to_string();
    };

    expect_error("/income/categories", "2");
    expect_error("/income/type", "ordinal");
    expect_error("/physical_activity/type", "complex");
    expect_error("/trend/type", "sideways");
    expect_error("/demographics/gender2", "other");
    expect_error("/demographics/max_age_for_linear_models", 0);
}

TEST(ConfigParsing, TwoStageLogisticNeedsItsFile) {
    const ConfigFixture fixture{"config_two_stage"};
    auto document = fixture.document();
    document["project_requirements"]["two_stage"]["use_logistic"] = true;

    auto [config, report] = load(fixture, document);
    EXPECT_FALSE(config.has_value());
    EXPECT_TRUE(report.contains(IssueCode::config_missing_required));

    document["project_requirements"]["two_stage"]["logistic_file"] = "logistic_regression.csv";
    auto [ok_config, ok_report] = load(fixture, document);
    ASSERT_TRUE(ok_config.has_value()) << ok_report.to_string();
    EXPECT_EQ("logistic_regression.csv", ok_config->project_requirements.two_stage.logistic_file);
}

TEST(ConfigParsing, RejectsTheV1FieldsThatV2Removed) {
    const ConfigFixture fixture{"config_removed_fields"};

    for (const auto *field : {"trend_type", "income_categories"}) {
        auto document = fixture.document();
        document[field] = "null";
        auto [config, report] = load(fixture, document);

        EXPECT_FALSE(config.has_value()) << field;
        ASSERT_TRUE(report.contains(IssueCode::config_removed_property));
        EXPECT_NE(std::string::npos, report.to_string().find("project_requirements"));
    }

    {
        auto document = fixture.document();
        document["running"]["sync_timeout_ms"] = 15000;
        auto [config, report] = load(fixture, document);

        EXPECT_FALSE(config.has_value());
        ASSERT_TRUE(report.contains(IssueCode::config_removed_property));
        EXPECT_NE(std::string::npos, report.to_string().find("journal"));
    }
}

TEST(ConfigParsing, RejectsUnknownPropertiesAndSuggestsNearMisses) {
    const ConfigFixture fixture{"config_unknown"};

    {
        auto document = fixture.document();
        document["running"]["start_tim"] = 2010;
        auto [config, report] = load(fixture, document);

        EXPECT_FALSE(config.has_value());
        ASSERT_TRUE(report.contains(IssueCode::config_unknown_property));
        EXPECT_NE(std::string::npos, report.to_string().find("did you mean 'start_time'"));
    }

    {
        auto document = fixture.document();
        document["nonsense"] = 1;
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_unknown_property));
    }
}

TEST(ConfigParsing, LoadsInputInfo) {
    const ConfigFixture fixture{"config_inputs"};

    {
        auto document = fixture.document();
        document["inputs"].erase("dataset");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        auto document = fixture.document();
        document["inputs"].erase("settings");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        auto document = fixture.document();
        document.erase("inputs");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        // A dataset file that does not exist is located and named.
        auto document = fixture.document();
        document["inputs"]["dataset"]["name"] = "not-here.csv";
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        ASSERT_TRUE(report.contains(IssueCode::file_not_found));

        bool names_the_dataset = false;
        for (const auto &issue : report.issues()) {
            names_the_dataset =
                names_the_dataset || issue.location.field == "/inputs/dataset/name";
        }
        EXPECT_TRUE(names_the_dataset) << report.to_string();
    }

    {
        auto document = fixture.document();
        document["inputs"]["settings"]["size_fraction"] = 0.0;
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        auto document = fixture.document();
        document["inputs"]["settings"]["age_range"] = nlohmann::json::array({100, 0});
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        auto document = fixture.document();
        document["inputs"]["settings"]["age_range"] = nlohmann::json::array({0});
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        auto document = fixture.document();
        document["inputs"]["dataset"]["columns"] = nlohmann::json::object();
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }
}

TEST(ConfigParsing, LoadsModellingInfo) {
    const ConfigFixture fixture{"config_modelling"};

    {
        auto document = fixture.document();
        document.erase("modelling");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        auto document = fixture.document();
        document["modelling"]["risk_factors"][0].erase("level");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        auto document = fixture.document();
        document["modelling"]["risk_factor_models"]["static"] = "absent.json";
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::file_not_found));
    }

    {
        auto document = fixture.document();
        document["modelling"]["baseline_adjustments"].erase("file_names");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        auto document = fixture.document();
        document["modelling"]["ses_model"].erase("function_name");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        auto document = fixture.document();
        document["modelling"]["ses_model"]["function_parameters"] =
            nlohmann::json::array({0.0, 0.0});
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        // A duplicated risk factor would give two models the same name.
        auto document = fixture.document();
        document["modelling"]["risk_factors"].push_back(
            nlohmann::json::parse(R"({"name": "bmi", "level": 3})"));
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }
}

TEST(ConfigParsing, PolicyStartYearIsOptional) {
    const ConfigFixture fixture{"config_policy_year"};

    auto [without, without_report] = load(fixture, fixture.document());
    ASSERT_TRUE(without.has_value()) << without_report.to_string();
    EXPECT_EQ(0U, without->modelling.policy_start_year);

    auto document = fixture.document();
    document["modelling"]["policy_start_year"] = 2024;
    auto [with, with_report] = load(fixture, document);
    ASSERT_TRUE(with.has_value()) << with_report.to_string();
    EXPECT_EQ(2024U, with->modelling.policy_start_year);
}

TEST(ConfigParsing, LoadsIncomeStratumFactorsMean) {
    const ConfigFixture fixture{"config_income_strata"};
    fixture.touch("male_q1.csv");
    fixture.touch("female_q1.csv");
    fixture.touch("male_q2.csv");
    fixture.touch("female_q2.csv");

    auto document = fixture.document();
    auto &block = document["modelling"]["baseline_adjustments"]["income_stratum_factors_mean"];
    block["enabled"] = true;
    block["adjustment_income_stratum_count"] = 2;
    block["strata"] = nlohmann::json::array(
        {{{"id", "Q1"}, {"factorsmean_male", "male_q1.csv"}, {"factorsmean_female", "female_q1.csv"}},
         {{"id", "Q2"}, {"factorsmean_male", "male_q2.csv"}, {"factorsmean_female", "female_q2.csv"}}});

    auto [config, report] = load(fixture, document);
    ASSERT_TRUE(config.has_value()) << report.to_string();

    const auto &strata = config->modelling.baseline_adjustments.income_stratum_factors_mean;
    EXPECT_TRUE(strata.enabled);
    EXPECT_EQ(2U, strata.adjustment_income_stratum_count);
    ASSERT_EQ(2U, strata.strata.size());
    EXPECT_EQ("Q1", strata.strata[0].id);
    EXPECT_EQ("Q2", strata.strata[1].id);
    EXPECT_EQ(fixture.dir() / "male_q2.csv", strata.strata[1].factorsmean_male);
}

TEST(ConfigParsing, IncomeStratumCountMustMatchWhenEnabled) {
    const ConfigFixture fixture{"config_income_strata_mismatch"};

    auto document = fixture.document();
    auto &block = document["modelling"]["baseline_adjustments"]["income_stratum_factors_mean"];
    block["enabled"] = true;
    block["adjustment_income_stratum_count"] = 2;
    block["strata"] = nlohmann::json::array();

    auto [config, report] = load(fixture, document);
    EXPECT_FALSE(config.has_value());
    EXPECT_TRUE(report.contains(IssueCode::config_bad_value));

    // A negative count is rejected outright.
    block["adjustment_income_stratum_count"] = -1;
    auto [negative, negative_report] = load(fixture, document);
    EXPECT_FALSE(negative.has_value());
    EXPECT_TRUE(negative_report.contains(IssueCode::config_bad_value));
}

TEST(ConfigParsing, IncomeStratumCountMayMismatchWhenDisabled) {
    // A config keeps the block while the feature is off, as the baseline allows.
    const ConfigFixture fixture{"config_income_strata_disabled"};
    fixture.touch("male_q1.csv");
    fixture.touch("female_q1.csv");

    auto document = fixture.document();
    auto &block = document["modelling"]["baseline_adjustments"]["income_stratum_factors_mean"];
    block["enabled"] = false;
    block["adjustment_income_stratum_count"] = 5;
    block["strata"] = nlohmann::json::array({{{"id", "Q1"},
                                              {"factorsmean_male", "male_q1.csv"},
                                              {"factorsmean_female", "female_q1.csv"}}});

    auto [config, report] = load(fixture, document);
    ASSERT_TRUE(config.has_value()) << report.to_string();

    const auto &strata = config->modelling.baseline_adjustments.income_stratum_factors_mean;
    EXPECT_FALSE(strata.enabled);
    EXPECT_EQ(5U, strata.adjustment_income_stratum_count);
    EXPECT_EQ(1U, strata.strata.size());
}

TEST(ConfigParsing, LoadsInterventions) {
    const ConfigFixture fixture{"config_interventions"};

    {
        // No active intervention: a baseline-only run.
        auto [config, report] = load(fixture, fixture.document());
        ASSERT_TRUE(config.has_value()) << report.to_string();
        EXPECT_FALSE(config->running.active_intervention.has_value());
    }

    {
        auto document = fixture.document();
        document["running"]["interventions"]["active_type_id"] = "simple";
        auto [config, report] = load(fixture, document);

        ASSERT_TRUE(config.has_value()) << report.to_string();
        ASSERT_TRUE(config->running.active_intervention.has_value());

        const auto &intervention = *config->running.active_intervention;
        EXPECT_EQ("simple", intervention.identifier);
        EXPECT_EQ(2022, intervention.active_period.start_time);
        ASSERT_TRUE(intervention.active_period.finish_time.has_value());
        EXPECT_EQ(2022, *intervention.active_period.finish_time);
        EXPECT_EQ("absolute", intervention.impact_type);
        ASSERT_EQ(1U, intervention.impacts.size());
        EXPECT_EQ("BMI", intervention.impacts.front().risk_factor);
        EXPECT_DOUBLE_EQ(-1.0, intervention.impacts.front().impact_value);
        EXPECT_EQ(0U, intervention.impacts.front().from_age);
        EXPECT_FALSE(intervention.impacts.front().to_age.has_value());
    }

    {
        // Matched case-insensitively, because the upstream examples are inconsistent.
        auto document = fixture.document();
        document["running"]["interventions"]["active_type_id"] = "Simple";
        auto [config, report] = load(fixture, document);
        ASSERT_TRUE(config.has_value()) << report.to_string();
        EXPECT_EQ("simple", config->running.active_intervention->identifier);
    }

    {
        auto document = fixture.document();
        document["running"]["interventions"]["active_type_id"] = "marketing";
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
        EXPECT_NE(std::string::npos, report.to_string().find("simple"));
    }

    {
        auto document = fixture.document();
        document["running"]["interventions"].erase("active_type_id");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        // An intervention that exists but is not implemented in this build stops the run with a
        // sentence rather than being silently ignored (ADR 0021).
        auto document = fixture.document();
        document["running"]["interventions"]["types"]["marketing"] =
            document["running"]["interventions"]["types"]["simple"];
        document["running"]["interventions"]["active_type_id"] = "marketing";
        auto [config, report] = load(fixture, document);

        EXPECT_FALSE(config.has_value());
        ASSERT_TRUE(report.contains(IssueCode::feature_not_implemented));
        EXPECT_NE(std::string::npos, report.to_string().find("backlog"));
    }

    {
        // Four of the six upstream examples select `simple` with an empty impact list, so this
        // loads — but it makes the intervention scenario a copy of the baseline scenario, which is
        // worth a warning nobody has to go looking for.
        auto document = fixture.document();
        document["running"]["interventions"]["types"]["simple"]["impacts"] =
            nlohmann::json::array();
        document["running"]["interventions"]["active_type_id"] = "simple";
        auto [config, report] = load(fixture, document);
        ASSERT_TRUE(config.has_value());
        EXPECT_EQ(0U, report.error_count());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
        EXPECT_NE(std::string::npos,
                  report.to_string().find("reproduce the baseline scenario"));
    }

    {
        // The baseline's PolicyPeriodNegativeStartThrows, moved to where a period can only come
        // from: the config.
        auto document = fixture.document();
        document["running"]["interventions"]["types"]["simple"]["active_period"]["start_time"] =
            -1;
        document["running"]["interventions"]["active_type_id"] = "simple";
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
        EXPECT_NE(std::string::npos, report.to_string().find("is not a calendar year"));
    }

    {
        auto document = fixture.document();
        document["running"]["interventions"]["types"]["simple"]["active_period"]["finish_time"] =
            2000;
        document["running"]["interventions"]["active_type_id"] = "simple";
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }
}

TEST(ConfigParsing, LoadsRunningInfo) {
    const ConfigFixture fixture{"config_running"};

    for (const auto *key : {"start_time", "stop_time", "diseases", "interventions"}) {
        auto document = fixture.document();
        document["running"][key] = nullptr;
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value()) << key;
        EXPECT_TRUE(report.has_errors());
    }

    {
        auto document = fixture.document();
        document.erase("running");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_missing_required));
    }

    {
        // trial_runs is optional, defaults to 1, and the default is reported.
        auto document = fixture.document();
        document["running"].erase("trial_runs");
        auto [config, report] = load(fixture, document);
        ASSERT_TRUE(config.has_value()) << report.to_string();
        EXPECT_EQ(1U, config->running.trial_runs);
        EXPECT_GE(report.warning_count(), 1U);
    }

    {
        auto document = fixture.document();
        document["running"]["trial_runs"] = 0;
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        auto document = fixture.document();
        document["running"]["diseases"] = nlohmann::json::array({"asthma", "Asthma"});
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        auto document = fixture.document();
        document["running"]["diseases"] = nlohmann::json::array();
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }
}

TEST(ConfigParsing, LoadsOutputInfo) {
    const ConfigFixture fixture{"config_output"};

    for (const auto *key : {"folder", "file_name", "comorbidities"}) {
        auto document = fixture.document();
        document["output"][key] = nullptr;
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value()) << key;
        EXPECT_TRUE(report.has_errors());
    }

    {
        // --output supplies the folder when the config leaves it empty.
        auto document = fixture.document();
        document["output"]["folder"] = "";
        LoadOptions options;
        options.output_folder = "/cli/folder";
        auto [config, report] = load(fixture, document, options);
        ASSERT_TRUE(config.has_value()) << report.to_string();
        EXPECT_EQ("/cli/folder", config->output.folder);
    }

    {
        // Both is ambiguous.
        LoadOptions options;
        options.output_folder = "/cli/folder";
        auto [config, report] = load(fixture, fixture.document(), options);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        // Neither is a missing output folder.
        auto document = fixture.document();
        document["output"]["folder"] = "";
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }
}

TEST(ConfigParsing, ExpandsEnvironmentVariablesInTheOutputFolderAndReportsUndefinedOnes) {
    const ConfigFixture fixture{"config_output_env"};
    ::setenv("HGPS_TEST_OUT", "/tmp/hgps-out", 1);

    auto document = fixture.document();
    document["output"]["folder"] = "${HGPS_TEST_OUT}/france";
    auto [config, report] = load(fixture, document);
    ASSERT_TRUE(config.has_value()) << report.to_string();
    EXPECT_EQ("/tmp/hgps-out/france", config->output.folder);
    ::unsetenv("HGPS_TEST_OUT");

    // Changed from the baseline, which expands an undefined variable to nothing (audit N-17).
    document["output"]["folder"] = "${HGPS_NOT_SET}/france";
    auto [bad, bad_report] = load(fixture, document);
    EXPECT_FALSE(bad.has_value());
    EXPECT_TRUE(bad_report.contains(IssueCode::config_undefined_variable));
}

TEST(ConfigParsing, LoadsIndividualIdTracking) {
    const ConfigFixture fixture{"config_tracking"};

    auto document = fixture.document();
    document["output"]["individual_id_tracking"] = nlohmann::json::parse(R"({
        "enabled": true, "age_min": 25, "age_max": 60, "gender": "all",
        "regions": [], "ethnicities": [], "risk_factors": ["bmi", "smoking"],
        "years": [2030, 2040], "scenarios": "both"
    })");

    auto [config, report] = load(fixture, document);
    ASSERT_TRUE(config.has_value()) << report.to_string();
    ASSERT_TRUE(config->output.individual_id_tracking.has_value());

    const auto &tracking = *config->output.individual_id_tracking;
    EXPECT_TRUE(tracking.enabled);
    EXPECT_EQ(25, tracking.age_min);
    EXPECT_EQ(60, tracking.age_max);
    EXPECT_EQ("all", tracking.gender);
    EXPECT_EQ(2U, tracking.risk_factors.size());
    EXPECT_EQ(2U, tracking.years.size());
    EXPECT_EQ("both", tracking.scenarios);
}

TEST(ConfigParsing, IndividualIdTrackingDefaultsAndValidation) {
    const ConfigFixture fixture{"config_tracking_defaults"};

    {
        auto document = fixture.document();
        document["output"]["individual_id_tracking"] =
            nlohmann::json::parse(R"({"enabled": false})");
        auto [config, report] = load(fixture, document);
        ASSERT_TRUE(config.has_value()) << report.to_string();
        ASSERT_TRUE(config->output.individual_id_tracking.has_value());
        EXPECT_FALSE(config->output.individual_id_tracking->enabled);
        EXPECT_EQ("all", config->output.individual_id_tracking->gender);
        EXPECT_EQ("both", config->output.individual_id_tracking->scenarios);
        EXPECT_TRUE(config->output.individual_id_tracking->years.empty());
    }

    {
        auto document = fixture.document();
        document["output"]["individual_id_tracking"] =
            nlohmann::json::parse(R"({"enabled": true, "gender": "other"})");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        auto document = fixture.document();
        document["output"]["individual_id_tracking"] =
            nlohmann::json::parse(R"({"enabled": true, "age_min": 60, "age_max": 25})");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
    }

    {
        // Absent entirely: no tracking, no diagnostics.
        auto [config, report] = load(fixture, fixture.document());
        ASSERT_TRUE(config.has_value());
        EXPECT_FALSE(config->output.individual_id_tracking.has_value());
    }
}

TEST(ConfigParsing, PopulationImpactFractionIsReservedAndRejectedWhenEnabled) {
    const ConfigFixture fixture{"config_pif"};

    {
        auto document = fixture.document();
        document["population_impact_fraction"] = nlohmann::json::parse(R"({"enabled": false})");
        auto [config, report] = load(fixture, document);
        EXPECT_TRUE(config.has_value()) << report.to_string();
    }

    {
        auto document = fixture.document();
        document["population_impact_fraction"] = nlohmann::json::parse(
            R"({"enabled": true, "data_root_path": "/d", "risk_factor": "Smoking",
                "scenario": "Scenario1"})");
        auto [config, report] = load(fixture, document);
        EXPECT_FALSE(config.has_value());
        EXPECT_TRUE(report.contains(IssueCode::feature_not_implemented));
    }
}

TEST(ConfigParsing, TheOutputFileNameIsUsedExactlyAsConfigured) {
    // Baseline finding B-08: the configured name is ignored unless it contains a {…} token, so
    // "result.csv" silently became HealthGPS_result_<timestamp>.csv. And N-15: a forced timestamp
    // means two runs of the same config never write comparable paths.
    hgps::config::Output output;
    output.file_name = "result.csv";
    EXPECT_EQ("result.csv", hgps::config::expand_output_file_name(output, 0));

    output.file_name = "france_{TIMESTAMP}.csv";
    const auto stamped = hgps::config::expand_output_file_name(output, 0);
    EXPECT_NE("france_{TIMESTAMP}.csv", stamped);
    EXPECT_TRUE(stamped.starts_with("france_"));
    EXPECT_TRUE(stamped.ends_with(".csv"));
    EXPECT_EQ(std::string::npos, stamped.find('{'));

    output.file_name = "run_{JOBID}.csv";
    EXPECT_EQ("run_7.csv", hgps::config::expand_output_file_name(output, 7));

    // A job id the name does not mention is appended, or every array element of an HPC job would
    // write to the same path.
    output.file_name = "result.csv";
    EXPECT_EQ("result_7.csv", hgps::config::expand_output_file_name(output, 7));
}

TEST(ConfigParsing, LoadsFromAFileAndRecordsItsHash) {
    const ConfigFixture fixture{"config_from_file"};
    const auto path = fixture.write(fixture.document());

    IssueReport report;
    const auto config = hgps::config::load(path, {}, report);

    ASSERT_TRUE(config.has_value()) << report.to_string();
    EXPECT_EQ(std::filesystem::absolute(path), config->source_path);
    EXPECT_EQ(64U, config->source_sha256.size());
    EXPECT_EQ(fixture.dir(), config->root_path);
}

TEST(ConfigParsing, AMalformedFileIsReportedWithALineNumber) {
    const ConfigFixture fixture{"config_malformed"};
    const auto path = fixture.dir() / "broken.json";
    {
        std::ofstream stream{path};
        stream << "{\n  \"version\": 2,\n";
    }

    IssueReport report;
    EXPECT_FALSE(hgps::config::load(path, {}, report).has_value());
    ASSERT_TRUE(report.contains(IssueCode::json_parse_error));
    EXPECT_TRUE(report.issues().front().location.line.has_value());
}
