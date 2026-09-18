// The command line is the one thing the CLI still owns after the library split, and while it lived
// inside main.cpp nothing tested it. These are the tests it should have had all along.
#include "options.h"

#include "hgps/baseline_compat.h"
#include "reporter.h"

#include <gtest/gtest.h>

#include <string>
#include <sstream>
#include <vector>

namespace {

hgps::app::OptionsResult parse(const std::vector<std::string> &arguments) {
    return hgps::app::parse_options(arguments);
}

} // namespace

TEST(CommandLine, AConfigIsRequired) {
    const auto result = parse({});
    EXPECT_FALSE(result.options.has_value());
    EXPECT_NE(std::string::npos, result.message.find("--config"));
}

TEST(CommandLine, TheLongAndShortConfigFlagsAgree) {
    const auto longer = parse({"--config", "run.json"});
    const auto shorter = parse({"-c", "run.json"});
    ASSERT_TRUE(longer.options.has_value());
    ASSERT_TRUE(shorter.options.has_value());
    EXPECT_EQ("run.json", longer.options->config.string());
    EXPECT_EQ(longer.options->config, shorter.options->config);
}

TEST(CommandLine, HelpAndVersionDoNotNeedAConfig) {
    const auto help = parse({"--help"});
    ASSERT_TRUE(help.options.has_value());
    EXPECT_TRUE(help.options->help);

    const auto version = parse({"--version"});
    ASSERT_TRUE(version.options.has_value());
    EXPECT_TRUE(version.options->version);
}

TEST(CommandLine, HelpWinsOverEverythingAfterIt) {
    // A user who types --help is asking a question, not starting a run, so parsing stops there
    // rather than going on to reject the rest of the line.
    const auto result = parse({"--help", "--not-an-argument"});
    ASSERT_TRUE(result.options.has_value());
    EXPECT_TRUE(result.options->help);
}

TEST(CommandLine, EveryFlagThatTakesAValueReportsAMissingOne) {
    for (const char *flag : {"--config", "-c", "--output", "-o", "--jobid", "-j", "--threads",
                             "-T"}) {
        const auto result = parse({flag});
        EXPECT_FALSE(result.options.has_value()) << flag;
        EXPECT_FALSE(result.message.empty()) << flag;
        EXPECT_NE(std::string::npos, result.message.find(flag)) << flag;
    }
}

TEST(CommandLine, ThreadsMustBeAtLeastOne) {
    EXPECT_FALSE(parse({"-c", "run.json", "--threads", "0"}).options.has_value());
    EXPECT_FALSE(parse({"-c", "run.json", "--threads", "-1"}).options.has_value());
    EXPECT_FALSE(parse({"-c", "run.json", "--threads", "two"}).options.has_value());

    const auto four = parse({"-c", "run.json", "--threads", "4"});
    ASSERT_TRUE(four.options.has_value());
    EXPECT_EQ(4U, four.options->threads);
}

TEST(CommandLine, TheThreadDefaultIsOne) {
    const auto result = parse({"-c", "run.json"});
    ASSERT_TRUE(result.options.has_value());
    EXPECT_EQ(1U, result.options->threads);
}

TEST(CommandLine, AJobIdMayBeZeroButNotNegative) {
    const auto zero = parse({"-c", "run.json", "--jobid", "0"});
    ASSERT_TRUE(zero.options.has_value());
    EXPECT_EQ(0, zero.options->job_id);

    EXPECT_FALSE(parse({"-c", "run.json", "--jobid", "-3"}).options.has_value());
}

TEST(CommandLine, TheBooleanFlagsAreOffUnlessGiven) {
    const auto bare = parse({"-c", "run.json"});
    ASSERT_TRUE(bare.options.has_value());
    EXPECT_FALSE(bare.options->dry_run);
    EXPECT_FALSE(bare.options->verbose);
    EXPECT_FALSE(bare.options->progress);

    const auto all = parse({"-c", "run.json", "--dry-run", "--verbose", "--progress"});
    ASSERT_TRUE(all.options.has_value());
    EXPECT_TRUE(all.options->dry_run);
    EXPECT_TRUE(all.options->verbose);
    EXPECT_TRUE(all.options->progress);
}

TEST(CommandLine, AnUnknownArgumentIsNamedInTheMessage) {
    const auto result = parse({"-c", "run.json", "--turbo"});
    EXPECT_FALSE(result.options.has_value());
    EXPECT_NE(std::string::npos, result.message.find("--turbo"));
}

TEST(CommandLine, TheOutputFolderIsOptional) {
    const auto bare = parse({"-c", "run.json"});
    ASSERT_TRUE(bare.options.has_value());
    EXPECT_FALSE(bare.options->output_folder.has_value());

    const auto given = parse({"-c", "run.json", "-o", "/tmp/results"});
    ASSERT_TRUE(given.options.has_value());
    ASSERT_TRUE(given.options->output_folder.has_value());
    EXPECT_EQ("/tmp/results", *given.options->output_folder);
}

TEST(CommandLine, TheUsageTextMentionsEveryFlagItAccepts) {
    // A flag the parser takes and the help does not mention is a feature nobody can find.
    const auto usage = hgps::app::usage_text();
    for (const char *flag : {"--config", "--output", "--dry-run", "--progress", "--jobid",
                             "--threads", "--verbose", "--version", "--help"}) {
        EXPECT_NE(std::string::npos, usage.find(flag)) << flag;
    }
}

TEST(VersionText, SaysWhatBuiltIt) {
    const auto text = hgps::app::version_text("healthgps");
    EXPECT_NE(std::string::npos, text.find("healthgps"));
    EXPECT_NE(std::string::npos, text.find("commit"));
    EXPECT_NE(std::string::npos, text.find("platform"));
    EXPECT_NE(std::string::npos, text.find(std::string{hgps::api::build_info().version}));
}

TEST(CommandLine, BaselineCompatIsOffUnlessAsked) {
    const auto result = parse({"-c", "run.json"});
    ASSERT_TRUE(result.options.has_value());
    EXPECT_TRUE(result.options->baseline_compat.none());
}

TEST(CommandLine, BaselineCompatTakesAFlagNameAndRepeats) {
    const auto once = parse({"-c", "run.json", "--baseline-compat", "B-24"});
    ASSERT_TRUE(once.options.has_value()) << once.message;
    EXPECT_TRUE(once.options->baseline_compat.is_set(hgps::api::CompatFlag::b24));

    // Repeating the same flag is not an error, and neither is asking for all of them twice.
    const auto twice = parse({"-c", "run.json", "--baseline-compat", "B-24",
                              "--baseline-compat", "all"});
    ASSERT_TRUE(twice.options.has_value()) << twice.message;
    EXPECT_EQ(hgps::api::BaselineCompat::flag_count, twice.options->baseline_compat.count());
}

TEST(CommandLine, AnUnknownBaselineCompatFlagIsRefusedWithTheListOfRealOnes) {
    const auto result = parse({"-c", "run.json", "--baseline-compat", "B-99"});
    EXPECT_FALSE(result.options.has_value());
    EXPECT_NE(std::string::npos, result.message.find("B-99"));
    EXPECT_NE(std::string::npos, result.message.find("B-24")) << result.message;

    const auto missing = parse({"-c", "run.json", "--baseline-compat"});
    EXPECT_FALSE(missing.options.has_value());
    EXPECT_NE(std::string::npos, missing.message.find("--baseline-compat"));
}

TEST(CommandLine, TheHelpTextMentionsEveryCompatibilityFlag) {
    const auto text = hgps::app::usage_text();
    EXPECT_NE(std::string::npos, text.find("--baseline-compat"));
    for (const auto flag : hgps::api::BaselineCompat::known()) {
        EXPECT_NE(std::string::npos, text.find(hgps::api::BaselineCompat::name_of(flag)))
            << hgps::api::BaselineCompat::name_of(flag) << " is missing from the help text";
    }
}

TEST(DryRunText, ReportsWhatTheRunWouldDo) {
    hgps::api::Run::Description description;
    description.disease_count = 6;
    description.risk_factor_count = 11;
    description.cohort_size = 6244;
    description.start_time = 2010;
    description.stop_time = 2050;
    description.seed = 123456789;

    const auto text = hgps::app::dry_run_text("healthgps", description);
    EXPECT_NE(std::string::npos, text.find("6 diseases"));
    EXPECT_NE(std::string::npos, text.find("11 risk factors"));
    EXPECT_NE(std::string::npos, text.find("6244"));
    EXPECT_NE(std::string::npos, text.find("2010"));
    EXPECT_NE(std::string::npos, text.find("123456789"));
}

TEST(DryRunText, SingularWhenThereIsOneOfSomething) {
    hgps::api::Run::Description description;
    description.disease_count = 1;
    description.risk_factor_count = 1;
    const auto text = hgps::app::dry_run_text("healthgps", description);
    EXPECT_NE(std::string::npos, text.find("1 disease,"));
    EXPECT_NE(std::string::npos, text.find("1 risk factor,"));
}

TEST(ConsoleReporter, PrintsTheSummaryToOutAndProgressToTheOtherStream) {
    std::ostringstream out;
    std::ostringstream progress;
    hgps::app::ConsoleReporter reporter{out, progress, true};

    reporter.on_run_started(hgps::api::RunStarted{.engine_version = "0.2.0",
                                                  .seed = 42,
                                                  .trial_runs = 1,
                                                  .start_time = 2020,
                                                  .stop_time = 2021,
                                                  .cohort_size = 100,
                                                  .scenarios = {"Baseline", "Intervention"},
                                                  .total_years = 4});
    reporter.on_year_completed(hgps::api::YearCompleted{.scenario = "Baseline",
                                                        .kind = hgps::api::ScenarioKind::baseline,
                                                        .run = 1,
                                                        .year = 2020,
                                                        .elapsed_ms = 12.0,
                                                        .population_size = 99});
    reporter.on_run_completed(hgps::api::RunCompleted{.elapsed_ms = 1500.0,
                                                      .cancelled = false,
                                                      .outputs = {"result.csv"}});

    EXPECT_NE(std::string::npos, progress.str().find("[1/4]"));
    EXPECT_NE(std::string::npos, progress.str().find("Baseline, Intervention"));
    EXPECT_EQ(std::string::npos, out.str().find("[1/4]"));
    EXPECT_NE(std::string::npos, out.str().find("result.csv"));
    EXPECT_NE(std::string::npos, out.str().find("1.5s"));
}

TEST(ConsoleReporter, PrintsNoProgressWhenItIsOff) {
    std::ostringstream out;
    std::ostringstream progress;
    hgps::app::ConsoleReporter reporter{out, progress, false};

    reporter.on_run_started(hgps::api::RunStarted{.total_years = 4});
    reporter.on_scenario_started(hgps::api::ScenarioStarted{.scenario = "Baseline"});
    reporter.on_year_completed(hgps::api::YearCompleted{.scenario = "Baseline", .year = 2020});
    reporter.on_scenario_completed(hgps::api::ScenarioCompleted{.scenario = "Baseline"});

    EXPECT_TRUE(progress.str().empty()) << progress.str();
    EXPECT_TRUE(out.str().empty()) << out.str();
}

TEST(ConsoleReporter, SaysSoWhenTheRunWasCancelled) {
    std::ostringstream out;
    std::ostringstream progress;
    hgps::app::ConsoleReporter reporter{out, progress, false};
    reporter.on_run_completed(hgps::api::RunCompleted{.elapsed_ms = 10.0, .cancelled = true});
    EXPECT_NE(std::string::npos, out.str().find("cancelled"));
}
