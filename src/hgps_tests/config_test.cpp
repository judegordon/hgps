#include "pch.h"

#include "hgps_input/config_parsing.h"
#include "hgps_input/config_section_parsing.h"
#include "hgps_input/json_parser.h"

#include <fstream>
#include <random>

using json = nlohmann::json;
using namespace hgps;
using namespace hgps::input;

namespace {

constexpr auto *POLICY_A = R"(
        {
            "active_period": {
                    "start_time": 2022,
                    "finish_time": 2022
                },
            "impact_type": "absolute",
            "impacts": [
                {
                    "risk_factor": "BMI",
                    "impact_value": -1.0,
                    "from_age": 0,
                    "to_age": null
                }
            ]
        })";

constexpr auto *POLICY_B = R"(
        {
            "active_period": {
                "start_time": 2022,
                "finish_time": 2050
            },
            "impacts": [
                {
                    "risk_factor": "BMI",
                    "impact_value": -0.12,
                    "from_age": 5,
                    "to_age": 12
                },
                {
                    "risk_factor": "BMI",
                    "impact_value": -0.31,
                    "from_age": 13,
                    "to_age": 18
                },
                {
                    "risk_factor": "BMI",
                    "impact_value": -0.16,
                    "from_age": 19,
                    "to_age": null
                }
            ]
        })";

const auto POLICIES = []() {
    json p;
    p["a"] = json::parse(POLICY_A);
    p["b"] = json::parse(POLICY_B);
    return p;
}();

class TempDir {
  public:
    TempDir() : rnd_{std::random_device()()} {
        path_ = std::filesystem::path{::testing::TempDir()} / "hgps" / random_string();
        if (!std::filesystem::create_directories(path_)) {
            throw std::runtime_error{"Could not create temp dir"};
        }

        path_ = std::filesystem::absolute(path_);
    }

    ~TempDir() {
        if (std::filesystem::exists(path_)) {
            std::filesystem::remove_all(path_);
        }
    }

    std::string random_string() const { return std::to_string(rnd_()); }

    const auto &path() const { return path_; }

  private:
    mutable std::mt19937 rnd_;
    std::filesystem::path path_;
};

class ConfigParsingFixture : public ::testing::Test {
  public:
    const auto &tmp_path() const { return dir_.path(); }

    std::filesystem::path random_filename() const { return dir_.random_string(); }

    std::filesystem::path create_file_relative() const {
        auto file_path = random_filename();
        std::ofstream ofs{dir_.path() / file_path};
        return file_path;
    }

    std::filesystem::path create_file_absolute() const {
        auto file_path = tmp_path() / random_filename();
        std::ofstream ofs{file_path};
        return file_path;
    }

    auto create_config() const {
        hgps::input::Configuration config;
        config.root_path = dir_.path();
        return config;
    }

  private:
    TempDir dir_;
};

} // anonymous namespace

TEST_F(ConfigParsingFixture, GetFileInfo) {
    const FileInfo info1{.name = create_file_absolute(),
                         .format = "csv",
                         .delimiter = ",",
                         .columns = {{"a", "string"}, {"b", "other string"}}};
    json j = info1;

    const auto info2 = get_file_info(j, tmp_path());
    EXPECT_EQ(info1, info2);

    j.erase("format");
    EXPECT_THROW(get_file_info(j, tmp_path()), ConfigurationError);
}

TEST_F(ConfigParsingFixture, GetBaseLineInfo) {
    const BaselineInfo info1{
        .format = "csv",
        .delimiter = ",",
        .encoding = "UTF8",
        .file_names = {{"a", create_file_absolute()}, {"b", create_file_absolute()}}};

    json j;
    j["baseline_adjustments"] = info1;

    const auto info2 = get_baseline_info(j, tmp_path());
    EXPECT_EQ(info1, info2);

    j["baseline_adjustments"]["file_names"]["a"] = random_filename();
    EXPECT_THROW(get_baseline_info(j, tmp_path()), ConfigurationError);
}

TEST_F(ConfigParsingFixture, LoadInterventions) {
    {
        auto config = create_config();
        json j;
        j["interventions"]["active_type_id"] = nullptr;
        j["interventions"]["types"] = json::object();
        EXPECT_NO_THROW(load_interventions(j, config));
        EXPECT_FALSE(config.active_intervention.has_value());
    }

    {
        auto config = create_config();
        json j;
        j["interventions"]["other_key"] = 1;
        j["interventions"]["types"] = json::object();
        EXPECT_THROW(load_interventions(j, config), ConfigurationError);
    }

    {
        auto config = create_config();
        json j;
        j["interventions"]["active_type_id"] = "A";
        j["interventions"]["types"] = POLICIES;
        EXPECT_NO_THROW(load_interventions(j, config));
        EXPECT_TRUE(config.active_intervention.has_value());
        auto intervention = POLICIES["a"].get<PolicyScenarioInfo>();
        intervention.identifier = "a";
        EXPECT_EQ(config.active_intervention, intervention);
    }

    {
        auto config = create_config();
        json j;
        j["interventions"]["active_type_id"] = "c";
        j["interventions"]["types"] = POLICIES;
        EXPECT_THROW(load_interventions(j, config), ConfigurationError);
    }
}

TEST_F(ConfigParsingFixture, LoadInputInfo) {
    const FileInfo file_info{.name = create_file_absolute(),
                             .format = "csv",
                             .delimiter = ",",
                             .columns = {{"a", "string"}, {"b", "other string"}}};
    const SettingsInfo settings_info{.country = "FRA",
                                     .age_range = hgps::core::IntegerInterval{0, 100},
                                     .size_fraction = 0.0001f};

    {
        auto config = create_config();
        json j;
        j["inputs"]["dataset"] = file_info;
        j["inputs"]["settings"] = settings_info;
        EXPECT_NO_THROW(load_input_info(j, config));
        EXPECT_EQ(config.file, file_info);
        EXPECT_EQ(config.settings, settings_info);
    }

    {
        json j;
        j["other_key"] = nullptr;
        auto config = create_config();
        EXPECT_THROW(load_input_info(j, config), ConfigurationError);
    }

    {
        auto config = create_config();
        json j;
        j["inputs"]["settings"] = settings_info;
        EXPECT_THROW(load_input_info(j, config), ConfigurationError);
    }

    {
        auto config = create_config();
        json j;
        j["inputs"]["dataset"] = file_info;
        EXPECT_THROW(load_input_info(j, config), ConfigurationError);
    }
}

TEST_F(ConfigParsingFixture, LoadModellingInfo) {
    const std::vector<RiskFactorInfo> risk_factors{
        RiskFactorInfo{.name = "Gender", .level = 0, .range = std::nullopt},
        RiskFactorInfo{.name = "Age", .level = 1, .range = hgps::core::DoubleInterval(0.0, 1.0)}};
    const std::unordered_map<std::string, std::filesystem::path> risk_factor_models{
        {"a", create_file_absolute()}, {"b", create_file_absolute()}};
    const BaselineInfo baseline_info{
        .format = "csv",
        .delimiter = ",",
        .encoding = "UTF8",
        .file_names = {{"a", create_file_absolute()}, {"b", create_file_absolute()}}};
    const SESInfo ses_info{.function = "normal", .parameters = {0.0, 1.0}};

    const json valid_modelling_info = [&]() {
        json j;
        j["modelling"]["risk_factors"] = risk_factors;
        j["modelling"]["risk_factor_models"] = risk_factor_models;
        j["modelling"]["baseline_adjustments"] = baseline_info;
        j["modelling"]["ses_model"] = ses_info;
        return j;
    }();

    {
        auto config = create_config();

        EXPECT_NO_THROW(load_modelling_info(valid_modelling_info, config));
        EXPECT_EQ(config.modelling.risk_factors, risk_factors);
        EXPECT_EQ(config.modelling.risk_factor_models, risk_factor_models);
        EXPECT_EQ(config.modelling.baseline_adjustment, baseline_info);
        EXPECT_EQ(config.ses, ses_info);
    }

    {
        auto config = create_config();
        json j;
        j["other_key"] = nullptr;
        EXPECT_THROW(load_modelling_info(j, config), ConfigurationError);
    }

    {
        auto config = create_config();
        auto j = valid_modelling_info;
        j["modelling"]["risk_factors"][0].erase("level");
        EXPECT_THROW(load_modelling_info(j, config), ConfigurationError);
    }

    {
        auto config = create_config();
        auto j = valid_modelling_info;
        j["modelling"]["risk_factor_models"]["a"] = random_filename();
        EXPECT_THROW(load_modelling_info(j, config), ConfigurationError);
    }

    {
        auto config = create_config();
        auto j = valid_modelling_info;
        j["modelling"]["baseline_adjustments"].erase("format");
        EXPECT_THROW(load_modelling_info(j, config), ConfigurationError);
    }

    {
        auto config = create_config();
        auto j = valid_modelling_info;
        j["modelling"]["ses_model"].erase("function_name");
        EXPECT_THROW(load_modelling_info(j, config), ConfigurationError);
    }
}

TEST_F(ConfigParsingFixture, LoadRunningInfo) {
    constexpr auto start_time = 2010u;
    constexpr auto stop_time = 2050u;
    constexpr auto trial_runs = 1u;
    constexpr auto sync_timeout_ms = 15'000u;
    constexpr auto seed = 42u;
    const std::vector<std::string> diseases{"alzheimer", "asthma",      "colorectalcancer",
                                            "diabetes",  "lowbackpain", "osteoarthritisknee"};

    const auto valid_running_info = [&]() {
        json j;
        auto &running = j["running"];
        running["start_time"] = start_time;
        running["stop_time"] = stop_time;
        running["trial_runs"] = trial_runs;
        running["sync_timeout_ms"] = sync_timeout_ms;
        running["diseases"] = diseases;
        running["seed"][0] = seed;
        running["interventions"]["active_type_id"] = "a";
        running["interventions"]["types"] = POLICIES;
        return j;
    }();

    {
        auto config = create_config();
        EXPECT_NO_THROW(load_running_info(valid_running_info, config));
        EXPECT_EQ(config.start_time, start_time);
        EXPECT_EQ(config.stop_time, stop_time);
        EXPECT_EQ(config.trial_runs, trial_runs);
        EXPECT_EQ(config.sync_timeout_ms, sync_timeout_ms);
        EXPECT_EQ(config.custom_seed, seed);
        EXPECT_EQ(config.diseases, diseases);
    }

    {
        auto config = create_config();
        auto j = valid_running_info;
        j["running"]["seed"].clear();

        EXPECT_NO_THROW(load_running_info(j, config));
        EXPECT_EQ(config.start_time, start_time);
        EXPECT_EQ(config.stop_time, stop_time);
        EXPECT_EQ(config.trial_runs, trial_runs);
        EXPECT_EQ(config.sync_timeout_ms, sync_timeout_ms);
        EXPECT_EQ(config.diseases, diseases);
        EXPECT_FALSE(config.custom_seed.has_value());
    }

    for (const auto *const key : {"start_time", "stop_time", "trial_runs", "sync_timeout_ms",
                                  "diseases", "seed", "interventions"}) {
        auto config = create_config();
        auto j = valid_running_info;
        j["running"][key] = nullptr;
        EXPECT_THROW(load_running_info(j, config), ConfigurationError);
    }
}

TEST_F(ConfigParsingFixture, LoadRunningInfoMissingRunningThrows) {
    json j;
    j["other"] = 1;
    auto config = create_config();
    EXPECT_THROW(load_running_info(j, config), ConfigurationError);
}

TEST_F(ConfigParsingFixture, LoadModellingInfoOptionalPolicyStartYear) {
    const std::vector<RiskFactorInfo> risk_factors{
        RiskFactorInfo{.name = "Age", .level = 1, .range = std::nullopt}};
    const std::unordered_map<std::string, std::filesystem::path> risk_factor_models{
        {"a", create_file_absolute()}};
    const BaselineInfo baseline_info{.format = "csv",
                                     .delimiter = ",",
                                     .encoding = "UTF8",
                                     .file_names = {{"a", create_file_absolute()}}};
    const SESInfo ses_info{.function = "normal", .parameters = {0.0}};
    json j;
    j["modelling"]["risk_factors"] = risk_factors;
    j["modelling"]["risk_factor_models"] = risk_factor_models;
    j["modelling"]["baseline_adjustments"] = baseline_info;
    j["modelling"]["ses_model"] = ses_info;

    auto config = create_config();
    EXPECT_NO_THROW(load_modelling_info(j, config));
    EXPECT_EQ(0u, config.modelling.policy_start_year);

    j["modelling"]["policy_start_year"] = 2024u;
    config = create_config();
    EXPECT_NO_THROW(load_modelling_info(j, config));
    EXPECT_EQ(2024u, config.modelling.policy_start_year);
}

TEST_F(ConfigParsingFixture, LoadOutputInfo) {
    const OutputInfo output_info{.comorbidities = 3,
                                 .folder = "/home/test",
                                 .file_name = "filename.txt",
                                 .individual_id_tracking = std::nullopt};
    const auto valid_output_info = [&]() {
        json j;
        j["output"] = output_info;
        return j;
    }();

    {
        auto config = create_config();
        EXPECT_NO_THROW(load_output_info(valid_output_info, config, std::nullopt));
        EXPECT_EQ(config.output, output_info);
    }

    for (const auto *const key : {"folder", "file_name", "comorbidities"}) {
        auto config = create_config();
        auto j = valid_output_info;
        j["output"][key] = nullptr;
        EXPECT_THROW(load_output_info(j, config, std::nullopt), ConfigurationError);
    }
}

TEST_F(ConfigParsingFixture, LoadOutputInfoWithIndividualIdTracking) {
    json j;
    j["output"] = {{"comorbidities", 3},
                   {"folder", "/home/test"},
                   {"file_name", "filename.txt"},
                   {"individual_id_tracking",
                    {{"enabled", true},
                     {"age_min", 25},
                     {"age_max", 60},
                     {"gender", "all"},
                     {"regions", json::array()},
                     {"ethnicities", json::array()},
                     {"risk_factors", json::array({"bmi", "smoking"})},
                     {"years", json::array({2030, 2040})},
                     {"scenarios", "both"}}}};

    Configuration config = create_config();
    EXPECT_NO_THROW(load_output_info(j, config, std::nullopt));
    ASSERT_TRUE(config.output.individual_id_tracking.has_value());
    EXPECT_TRUE(config.output.individual_id_tracking->enabled);
    EXPECT_EQ(25, config.output.individual_id_tracking->age_min);
    EXPECT_EQ(60, config.output.individual_id_tracking->age_max);
    EXPECT_EQ("all", config.output.individual_id_tracking->gender);
    EXPECT_EQ(2u, config.output.individual_id_tracking->risk_factors.size());
    EXPECT_EQ(2u, config.output.individual_id_tracking->years.size());
    EXPECT_EQ("both", config.output.individual_id_tracking->scenarios);
}

TEST_F(ConfigParsingFixture, LoadOutputInfoWithIndividualIdTrackingDisabled) {
    json j;
    j["output"] = {{"comorbidities", 2},
                   {"folder", "/tmp/out"},
                   {"file_name", "out.txt"},
                   {"individual_id_tracking", {{"enabled", false}}}};
    Configuration config = create_config();
    EXPECT_NO_THROW(load_output_info(j, config, std::nullopt));
    ASSERT_TRUE(config.output.individual_id_tracking.has_value());
    EXPECT_FALSE(config.output.individual_id_tracking->enabled);
}

TEST_F(ConfigParsingFixture, LoadOutputInfoOutputFolderFromCli) {
    json j;
    j["output"] = {{"comorbidities", 3}, {"folder", ""}, {"file_name", "f.txt"}};
    Configuration config = create_config();
    EXPECT_NO_THROW(load_output_info(j, config, std::string("/cli/folder")));
    EXPECT_EQ(config.output.folder, "/cli/folder");
}

TEST_F(ConfigParsingFixture, LoadOutputInfoOutputFolderBothSpecifiedThrows) {
    json j;
    j["output"] = {{"comorbidities", 3}, {"folder", "/config/folder"}, {"file_name", "f.txt"}};
    Configuration config = create_config();
    EXPECT_THROW(load_output_info(j, config, std::string("/cli/folder")), ConfigurationError);
}

TEST_F(ConfigParsingFixture, LoadOutputInfoOutputFolderNeitherSpecifiedThrows) {
    json j;
    j["output"] = {{"comorbidities", 3}, {"folder", ""}, {"file_name", "f.txt"}};
    Configuration config = create_config();
    EXPECT_THROW(load_output_info(j, config, std::nullopt), ConfigurationError);
}