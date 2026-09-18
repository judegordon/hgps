// The public API, exercised as a caller sees it: four calls, opaque handles, a report that
// accumulates.
//
// These tests include `hgps/engine.h` and nothing internal, on purpose — they are the closest thing
// the suite has to a second host, and the only place the published surface is checked for being
// usable rather than merely compilable.
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include "hgps/engine.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

namespace {

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

hgps::api::LoadOptions into(const std::filesystem::path &folder) {
    hgps::api::LoadOptions options;
    options.output_folder_override = folder.string();
    return options;
}

} // namespace

TEST(PublicApi, TheFourStepsSucceedOnTheSyntheticExample) {
    const auto folder = hgps::test::scratch_dir("api_four_steps");
    hgps::api::Report report;

    auto configuration =
        hgps::api::load_configuration(hgps::test::synthetic_config(), into(folder), report);
    ASSERT_TRUE(configuration.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors());

    const auto data = hgps::api::resolve_data(*configuration, report);
    ASSERT_TRUE(data.has_value()) << report.to_string();
    EXPECT_TRUE(std::filesystem::is_directory(data->directory()));

    auto run = hgps::api::build_run(*configuration, *data, report);
    ASSERT_TRUE(run.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors()) << report.to_string();

    const auto summary = hgps::api::execute(*run, hgps::api::RunOptions{}, report);
    EXPECT_TRUE(summary.succeeded);
    EXPECT_FALSE(summary.cancelled);
    EXPECT_GT(summary.years_completed, 0U);
    EXPECT_TRUE(std::filesystem::is_regular_file(summary.result_csv));
    EXPECT_TRUE(std::filesystem::is_regular_file(summary.result_json));
}

TEST(PublicApi, TheConfigurationAnswersWhatACallerNeedsBeforeRunning) {
    const auto folder = hgps::test::scratch_dir("api_configuration");
    hgps::api::Report report;
    auto configuration =
        hgps::api::load_configuration(hgps::test::synthetic_config(), into(folder), report);
    ASSERT_TRUE(configuration.has_value()) << report.to_string();

    EXPECT_EQ(hgps::test::synthetic_config(), configuration->path());
    EXPECT_EQ(64U, configuration->sha256().size()) << configuration->sha256();
    EXPECT_NE(0U, configuration->seed());
    EXPECT_LT(configuration->start_time(), configuration->stop_time());
    EXPECT_GE(configuration->trial_runs(), 1U);
    EXPECT_FALSE(configuration->diseases().empty());
    EXPECT_FALSE(configuration->data_source().empty());
    EXPECT_EQ(folder, configuration->output_folder());
    EXPECT_FALSE(configuration->output_file_name().empty());
}

TEST(PublicApi, TheConfigurationSha256IsTheFilesBytes) {
    const auto folder = hgps::test::scratch_dir("api_sha_bytes");
    hgps::api::Report report;
    auto configuration =
        hgps::api::load_configuration(hgps::test::synthetic_config(), into(folder), report);
    ASSERT_TRUE(configuration.has_value()) << report.to_string();

    // Two loads of the same file agree, and that is what a stored equivalence reference and a run
    // manifest are keyed by.
    hgps::api::Report second_report;
    auto second = hgps::api::load_configuration(hgps::test::synthetic_config(),
                                                into(hgps::test::scratch_dir("api_sha_bytes_2")),
                                                second_report);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(configuration->sha256(), second->sha256());
}

TEST(PublicApi, TheRunDescriptionMatchesTheConfiguration) {
    const auto folder = hgps::test::scratch_dir("api_description");
    hgps::api::Report report;
    auto configuration =
        hgps::api::load_configuration(hgps::test::synthetic_config(), into(folder), report);
    ASSERT_TRUE(configuration.has_value()) << report.to_string();
    const auto data = hgps::api::resolve_data(*configuration, report);
    ASSERT_TRUE(data.has_value()) << report.to_string();
    auto run = hgps::api::build_run(*configuration, *data, report);
    ASSERT_TRUE(run.has_value()) << report.to_string();

    const auto &description = run->description();
    EXPECT_EQ(configuration->seed(), description.seed);
    EXPECT_EQ(configuration->start_time(), description.start_time);
    EXPECT_EQ(configuration->stop_time(), description.stop_time);
    EXPECT_EQ(configuration->trial_runs(), description.trial_runs);
    EXPECT_EQ(configuration->diseases().size(), description.disease_count);
    EXPECT_EQ(configuration->trial_runs(), description.run_seeds.size());
    EXPECT_FALSE(description.country.empty());
    EXPECT_GT(description.cohort_size, 0U);
    EXPECT_GT(description.risk_factor_count, 0U);

    // A baseline-only configuration announces one scenario; one with an intervention announces two,
    // and the names are the ones the events will carry.
    ASSERT_FALSE(description.scenarios.empty());
    EXPECT_EQ("Baseline", description.scenarios.front());
    EXPECT_EQ(configuration->active_intervention().has_value() ? 2U : 1U,
              description.scenarios.size());
}

TEST(PublicApi, AnInterventionAddsASecondAnnouncedScenario) {
    auto document = hgps::test::synthetic_config_document();
    document["running"]["interventions"]["active_type_id"] = "simple";
    const auto config = hgps::test::write_config_variant("api_intervention", document);

    const auto folder = hgps::test::scratch_dir("api_intervention_out");
    hgps::api::Report report;
    auto configuration = hgps::api::load_configuration(config, into(folder), report);
    ASSERT_TRUE(configuration.has_value()) << report.to_string();
    ASSERT_TRUE(configuration->active_intervention().has_value());
    EXPECT_EQ("simple", *configuration->active_intervention());

    const auto data = hgps::api::resolve_data(*configuration, report);
    ASSERT_TRUE(data.has_value()) << report.to_string();
    auto run = hgps::api::build_run(*configuration, *data, report);
    ASSERT_TRUE(run.has_value()) << report.to_string();

    ASSERT_EQ(2U, run->description().scenarios.size());
    EXPECT_EQ("Baseline", run->description().scenarios[0]);
    EXPECT_EQ("Intervention", run->description().scenarios[1]);
}

TEST(PublicApi, ABadConfigurationComesBackAsDiagnosticsNotAnException) {
    const auto missing = hgps::test::scratch_dir("api_missing") / "nope.json";
    hgps::api::Report report;

    const auto configuration = hgps::api::load_configuration(missing, hgps::api::LoadOptions{},
                                                             report);
    EXPECT_FALSE(configuration.has_value());
    EXPECT_TRUE(report.has_errors());
    EXPECT_GE(report.error_count(), 1U);
    EXPECT_TRUE(report.contains("file_not_found")) << report.to_string();

    // A caller can show this to a user as it stands.
    EXPECT_NE(std::string::npos, report.to_string().find("nope.json"));
}

TEST(PublicApi, EveryDiagnosticCarriesACodeAndSomewhereToLook) {
    auto document = hgps::test::synthetic_config_document();
    document["running"]["stop_time"] = document["running"]["start_time"];
    document["running"]["turbo"] = true;
    const auto config = hgps::test::write_config_variant("api_diagnostics", document);

    hgps::api::Report report;
    const auto configuration = hgps::api::load_configuration(config, hgps::api::LoadOptions{},
                                                             report);
    EXPECT_FALSE(configuration.has_value());
    ASSERT_FALSE(report.diagnostics().empty());

    for (const auto &diagnostic : report.diagnostics()) {
        EXPECT_FALSE(diagnostic.code.empty());
        EXPECT_FALSE(diagnostic.message.empty());
        EXPECT_FALSE(diagnostic.location.empty()) << diagnostic.to_string();
        EXPECT_NE(std::string::npos, diagnostic.to_string().find(diagnostic.code));
    }
    EXPECT_TRUE(report.contains("config_unknown_property")) << report.to_string();
}

TEST(PublicApi, AWarningComesBackAlongsideAUsableConfiguration) {
    // The synthetic config's one diagnostic is the documented gender2 default, which is a warning:
    // the load succeeds and the report is not empty, and a caller that only checks the optional
    // never sees it.
    const auto folder = hgps::test::scratch_dir("api_warning");
    hgps::api::Report report;
    const auto configuration =
        hgps::api::load_configuration(hgps::test::synthetic_config(), into(folder), report);

    ASSERT_TRUE(configuration.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors());
    EXPECT_GE(report.warning_count(), 1U);
    EXPECT_TRUE(report.contains("config_default_applied")) << report.to_string();
}

TEST(PublicApi, TheTwoOutputFolderOptionsAreNotBothAllowed) {
    const auto folder = hgps::test::scratch_dir("api_two_folders");
    hgps::api::LoadOptions options;
    options.output_folder = folder.string();
    options.output_folder_override = folder.string();

    hgps::api::Report report;
    const auto configuration =
        hgps::api::load_configuration(hgps::test::synthetic_config(), options, report);
    EXPECT_FALSE(configuration.has_value());
    EXPECT_NE(std::string::npos, report.to_string().find("one place")) << report.to_string();
}

TEST(PublicApi, TheCommandLineStyleOverrideStillRefusesADoubledFolder) {
    // The synthetic config names an output folder of its own, so `--output` is the mistake the
    // loader has always reported. The host override is the one that may replace it.
    hgps::api::LoadOptions options;
    options.output_folder = hgps::test::scratch_dir("api_cli_folder").string();

    hgps::api::Report report;
    const auto configuration =
        hgps::api::load_configuration(hgps::test::synthetic_config(), options, report);
    EXPECT_FALSE(configuration.has_value());
    EXPECT_NE(std::string::npos, report.to_string().find("command line")) << report.to_string();
}

TEST(PublicApi, ARunCanBeExecutedWithoutASubscriberOrAToken) {
    const auto folder = hgps::test::scratch_dir("api_no_subscriber");
    hgps::api::Report report;
    auto configuration =
        hgps::api::load_configuration(hgps::test::synthetic_config(), into(folder), report);
    ASSERT_TRUE(configuration.has_value()) << report.to_string();
    const auto data = hgps::api::resolve_data(*configuration, report);
    ASSERT_TRUE(data.has_value());
    auto run = hgps::api::build_run(*configuration, *data, report);
    ASSERT_TRUE(run.has_value()) << report.to_string();

    const auto summary = hgps::api::execute(*run, hgps::api::RunOptions{}, report);
    EXPECT_TRUE(summary.succeeded);
    EXPECT_FALSE(read_file(summary.result_csv).empty());
}

TEST(BuildInfo, IsFilledIn) {
    const auto &build = hgps::api::build_info();
    EXPECT_FALSE(build.version.empty());
    EXPECT_EQ(build.version, hgps::api::version());
    EXPECT_FALSE(build.platform.empty());
    EXPECT_FALSE(build.compiler.empty());

    // "unknown" is allowed — a source tarball has no git metadata — but empty is not, because an
    // empty field in a manifest reads as "nobody looked".
    EXPECT_FALSE(build.git_commit.empty());
    EXPECT_FALSE(build.git_describe.empty());
}

TEST(PublicReport, CountsAndFormatsWhatItHolds) {
    hgps::api::Report report;
    EXPECT_TRUE(report.empty());
    EXPECT_FALSE(report.has_errors());
    EXPECT_TRUE(report.to_string().empty());

    report.add(hgps::api::Diagnostic{.severity = hgps::api::Severity::warning,
                                     .code = "config_default_applied",
                                     .location = {.file = "a.json", .field = "/x"},
                                     .message = "assumed 1"});
    report.add(hgps::api::Diagnostic{.severity = hgps::api::Severity::error,
                                     .code = "config_bad_value",
                                     .location = {.file = "a.json", .line = 7U, .column = 3U},
                                     .message = "not a number"});

    EXPECT_EQ(1U, report.warning_count());
    EXPECT_EQ(1U, report.error_count());
    EXPECT_TRUE(report.has_errors());
    EXPECT_TRUE(report.contains("config_bad_value"));
    EXPECT_FALSE(report.contains("file_not_found"));

    const auto text = report.to_string();
    EXPECT_NE(std::string::npos, text.find("a.json:7:3"));
    EXPECT_NE(std::string::npos, text.find("(/x)"));
    EXPECT_NE(std::string::npos, text.find("1 error, 1 warning"));

    hgps::api::Report other;
    other.merge(report);
    EXPECT_EQ(2U, other.diagnostics().size());
    EXPECT_EQ(report.to_string(), other.to_string());

    other.clear();
    EXPECT_TRUE(other.empty());
    EXPECT_EQ(0U, other.error_count());
}
