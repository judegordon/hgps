// The run manifest. A result file with no record of what produced it is a table of numbers, so what
// these tests check is that every field a reader would need to reproduce the run is present and
// correct — including the ones whose honest value is "there isn't one".
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include "hgps/engine.h"

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

namespace {

nlohmann::json read_json(const std::filesystem::path &path) {
    std::ifstream stream{path};
    return nlohmann::json::parse(stream);
}

} // namespace

TEST(RunManifest, IsWrittenBesideTheResults) {
    const auto folder = hgps::test::scratch_dir("manifest_beside");
    const auto outcome = hgps::test::run_simulation(hgps::test::synthetic_config(), folder);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    ASSERT_FALSE(outcome.manifest_path.empty());
    EXPECT_TRUE(std::filesystem::is_regular_file(outcome.manifest_path));
    EXPECT_EQ(outcome.csv_path.parent_path(), outcome.manifest_path.parent_path());
    EXPECT_EQ("result_manifest.json", outcome.manifest_path.filename().string());
}

TEST(RunManifest, TheResultCsvIsUnchangedByTheManifestExisting) {
    // The manifest is a new file, not a new column: the CSV is the thing every downstream script
    // reads, and adding provenance must not touch it.
    const auto outcome = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                   hgps::test::scratch_dir("manifest_csv"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    std::ifstream stream{outcome.csv_path};
    std::string header;
    ASSERT_TRUE(std::getline(stream, header));
    EXPECT_EQ(std::string::npos, header.find("manifest"));
    EXPECT_EQ(std::string::npos, header.find("git"));
}

TEST(RunManifest, RecordsEverythingNeededToReproduceTheRun) {
    const auto folder = hgps::test::scratch_dir("manifest_fields");
    const auto outcome = hgps::test::run_simulation(hgps::test::synthetic_config(), folder, 2);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto manifest = read_json(outcome.manifest_path);

    EXPECT_EQ(1, manifest.at("manifest_version").get<int>());

    // The config, by path and by content hash. The hash is what ties a result to exact inputs.
    EXPECT_FALSE(manifest.at("config").at("path").get<std::string>().empty());
    EXPECT_EQ(64U, manifest.at("config").at("sha256").get<std::string>().size());

    // The data, by source and by resolved directory.
    EXPECT_FALSE(manifest.at("data").at("source").get<std::string>().empty());
    EXPECT_TRUE(std::filesystem::is_directory(
        manifest.at("data").at("directory").get<std::string>()));

    // The seed actually used — not one the config might have had — and each run's derived seed.
    EXPECT_NE(0U, manifest.at("seed").get<std::uint32_t>());
    EXPECT_EQ(manifest.at("run").at("trial_runs").get<unsigned int>(),
              manifest.at("run_seeds").size());

    // What built the binary.
    const auto &engine = manifest.at("engine");
    EXPECT_EQ(std::string{hgps::api::build_info().version},
              engine.at("version").get<std::string>());
    EXPECT_FALSE(engine.at("git_commit").get<std::string>().empty());
    EXPECT_FALSE(engine.at("platform").get<std::string>().empty());
    EXPECT_FALSE(engine.at("compiler").get<std::string>().empty());
    EXPECT_TRUE(engine.at("git_dirty").is_boolean());

    // When, and for how long.
    const auto &timing = manifest.at("timing");
    EXPECT_EQ(20U, timing.at("started_utc").get<std::string>().size()) << "ISO-8601 UTC to the second";
    EXPECT_TRUE(timing.at("started_utc").get<std::string>().ends_with("Z"));
    EXPECT_TRUE(timing.at("finished_utc").get<std::string>().ends_with("Z"));
    EXPECT_GT(timing.at("elapsed_ms").get<double>(), 0.0);

    // What ran.
    EXPECT_EQ(nlohmann::json::array({"Baseline"}), manifest.at("scenarios"));
    const auto &run = manifest.at("run");
    EXPECT_EQ("Synthland", run.at("country").get<std::string>());
    EXPECT_LT(run.at("start_time").get<int>(), run.at("stop_time").get<int>());
    EXPECT_GT(run.at("cohort_size").get<std::size_t>(), 0U);
    EXPECT_EQ(2U, run.at("threads").get<std::size_t>());
    EXPECT_FALSE(run.at("cancelled").get<bool>());
    EXPECT_EQ(outcome.years_completed, run.at("years_completed").get<std::size_t>());

    // And what it wrote, by name, so the manifest identifies its own set of files.
    std::set<std::string> results;
    for (const auto &name : manifest.at("results")) {
        results.insert(name.get<std::string>());
    }
    EXPECT_TRUE(results.contains("result.csv"));
    EXPECT_TRUE(results.contains("result.json"));
    EXPECT_FALSE(results.contains("result_manifest.json"))
        << "the manifest does not list itself; it is the thing doing the listing";
}

TEST(RunManifest, TheSeedItRecordsIsTheSeedTheResultsUsed) {
    const auto outcome = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                   hgps::test::scratch_dir("manifest_seed"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto manifest = read_json(outcome.manifest_path);
    const auto results = read_json(outcome.json_path);

    // The two files are written by different objects from the same numbers, and this is the audit
    // finding that made that worth checking: the baseline writes `seed().value_or(0)` in one of them
    // (B-06), so an unseeded run claims seed 0 and re-running with 0 does not reproduce it.
    EXPECT_EQ(results.at("metadata").at("seed").get<std::uint32_t>(),
              manifest.at("seed").get<std::uint32_t>());
    EXPECT_EQ(results.at("metadata").at("run_seeds"), manifest.at("run_seeds"));
    EXPECT_EQ(results.at("metadata").at("config_sha256").get<std::string>(),
              manifest.at("config").at("sha256").get<std::string>());
}

TEST(RunManifest, SaysWhyTheChecksumIsMissingRatherThanLeavingItOut) {
    // The synthetic pack is a directory, which has no single archive hash. An absent key would read
    // as "nobody recorded it"; a null with a note says which of the two it is.
    const auto outcome = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                   hgps::test::scratch_dir("manifest_checksum"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto manifest = read_json(outcome.manifest_path);
    ASSERT_TRUE(manifest.at("data").contains("checksum"));
    EXPECT_TRUE(manifest.at("data").at("checksum").is_null());
    EXPECT_NE(std::string::npos,
              manifest.at("data").at("checksum_note").get<std::string>().find("directory"));
}

TEST(RunManifest, RecordsBothScenariosWhenAnInterventionIsActive) {
    auto document = hgps::test::synthetic_config_document();
    document["running"]["interventions"]["active_type_id"] = "simple";
    const auto config = hgps::test::write_config_variant("manifest_intervention", document);

    const auto outcome =
        hgps::test::run_simulation(config, hgps::test::scratch_dir("manifest_intervention_out"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto manifest = read_json(outcome.manifest_path);
    EXPECT_EQ(nlohmann::json::array({"Baseline", "Intervention"}), manifest.at("scenarios"));
}

TEST(RunManifest, SaysSoWhenTheRunWasCancelled) {
    hgps::api::CancellationToken token;
    token.cancel();

    const auto outcome =
        hgps::test::run_simulation(hgps::test::synthetic_config(),
                                   hgps::test::scratch_dir("manifest_cancelled"), 1, nullptr,
                                   token);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto manifest = read_json(outcome.manifest_path);
    EXPECT_TRUE(manifest.at("run").at("cancelled").get<bool>());
    EXPECT_EQ(0U, manifest.at("run").at("years_completed").get<std::size_t>());
}

TEST(RunManifest, TwoRunsOfTheSameConfigAgreeOnEverythingButTheClock) {
    // The manifest is not byte-identical between runs and must not be: it records when the run
    // happened and how long it took. Everything else is a property of the inputs and the binary, so
    // everything else has to match — which is the same rule the result CSV follows by carrying no
    // timestamp at all (N-15).
    const auto first = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                 hgps::test::scratch_dir("manifest_twice_a"));
    const auto second = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                  hgps::test::scratch_dir("manifest_twice_b"));
    ASSERT_TRUE(first.succeeded);
    ASSERT_TRUE(second.succeeded);

    auto left = read_json(first.manifest_path);
    auto right = read_json(second.manifest_path);

    // The output folder differs by construction, so the two paths that carry it are dropped too.
    for (auto *document : {&left, &right}) {
        document->erase("timing");
    }
    EXPECT_EQ(left, right);
}
