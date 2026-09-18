// The compatibility flag, end to end: it must reach the engine, change the numbers in the way the
// deviation predicts, and be recorded in the manifest of the run that used it (ADR 0041).
//
// The unit tests in tests/sim/interventions_test.cpp pin the one statement B-24 selects. These
// check that a flag written in a config document arrives there at all — the part a unit test on
// the scenario class cannot see.
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include "hgps/engine.h"

#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

nlohmann::json read_json(const std::filesystem::path &path) {
    std::ifstream stream{path};
    return nlohmann::json::parse(stream);
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// @brief The synthetic config with `food_labelling` active — the one policy B-24 lives in — and
///        whatever compatibility flags are asked for.
nlohmann::json food_labelling_document(const nlohmann::json &compat_flags) {
    auto document = hgps::test::synthetic_config_document();
    auto definition = document["running"]["interventions"]["types"]["simple"];

    // Two things make the deviation reachable. A coverage rate strictly between 0 and 1, because
    // it needs somebody who fails one draw and passes a later one — at a rate of 1 nobody ever
    // fails. And at least three years in which the policy runs: one to fail in, one to pass in,
    // and one to be wrongly offered it again in. The synthetic config's horizon is 2010–2014 and
    // its risk factors are updated in every year but the first, so the policy starts here in 2011
    // rather than the fixture's 2012, which would leave it only two.
    definition["active_period"]["start_time"] = 2011;
    definition["coverage_rates"] = {0.3, 0.6};
    definition["coverage_cutoff_time"] = 20;
    definition["child_cutoff_age"] = 18;
    definition["coefficients"] = {0.1, 0.11, 0.12, 0.13};
    definition["adjustments"] = {{{"risk_factor", "Energy"}, {"value", 0.25}}};
    definition["impacts"] = {{{"risk_factor", "BMI"},
                              {"impact_value", -0.05},
                              {"from_age", 5},
                              {"to_age", nullptr}}};

    document["running"]["interventions"]["types"]["food_labelling"] = definition;
    document["running"]["interventions"]["active_type_id"] = "food_labelling";
    if (!compat_flags.empty()) {
        document["baseline_compat"] = compat_flags;
    }
    return document;
}

} // namespace

TEST(BaselineCompatEndToEnd, AnOrdinaryRunRecordsNoFlags) {
    const auto outcome = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                   hgps::test::scratch_dir("compat_none"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto manifest = read_json(outcome.manifest_path);
    // Present and empty rather than absent: "which deviations did this run put back?" is a
    // question every manifest answers.
    ASSERT_TRUE(manifest.contains("baseline_compat"));
    EXPECT_TRUE(manifest.at("baseline_compat").is_array());
    EXPECT_TRUE(manifest.at("baseline_compat").empty());
}

TEST(BaselineCompatEndToEnd, TheManifestNamesTheFlagsTheRunUsed) {
    const auto config = hgps::test::write_config_variant(
        "compat_manifest_config", food_labelling_document(nlohmann::json::array({"B-24"})));

    const auto outcome =
        hgps::test::run_simulation(config, hgps::test::scratch_dir("compat_manifest_out"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto manifest = read_json(outcome.manifest_path);
    EXPECT_EQ(nlohmann::json::array({"B-24"}), manifest.at("baseline_compat"));
}

TEST(BaselineCompatEndToEnd, TheFlagChangesTheResultsAndNothingElseDoes) {
    // The whole point of the flag: with it on the output is the baseline's, with it off it is this
    // build's, and the two differ. If they did not, the flag would not be reaching the engine and
    // every "deviation impact" figure the harness reports would be zero for the wrong reason.
    const auto fixed = hgps::test::write_config_variant(
        "compat_fixed_config", food_labelling_document(nlohmann::json::array()));
    const auto restored = hgps::test::write_config_variant(
        "compat_restored_config", food_labelling_document(nlohmann::json::array({"B-24"})));

    const auto fixed_run =
        hgps::test::run_simulation(fixed, hgps::test::scratch_dir("compat_fixed_out"));
    ASSERT_TRUE(fixed_run.succeeded) << fixed_run.report.to_string();
    const auto restored_run =
        hgps::test::run_simulation(restored, hgps::test::scratch_dir("compat_restored_out"));
    ASSERT_TRUE(restored_run.succeeded) << restored_run.report.to_string();

    EXPECT_NE(read_file(fixed_run.csv_path), read_file(restored_run.csv_path));
}

TEST(BaselineCompatEndToEnd, TheFlagIsStillDeterministic) {
    // A run with a compatibility flag on is an ordinary run in every other respect, so it keeps
    // the determinism contract: same config, same seed, byte-identical output.
    const auto config = hgps::test::write_config_variant(
        "compat_determinism_config", food_labelling_document(nlohmann::json::array({"all"})));

    const auto first =
        hgps::test::run_simulation(config, hgps::test::scratch_dir("compat_det_a"), 1);
    const auto second =
        hgps::test::run_simulation(config, hgps::test::scratch_dir("compat_det_b"), 4);
    ASSERT_TRUE(first.succeeded) << first.report.to_string();
    ASSERT_TRUE(second.succeeded) << second.report.to_string();

    EXPECT_EQ(read_file(first.csv_path), read_file(second.csv_path));
}

TEST(BaselineCompatEndToEnd, TheFlagIsPartOfWhatALoadedConfigurationReports) {
    const auto config = hgps::test::write_config_variant(
        "compat_reported_config", food_labelling_document(nlohmann::json::array({"B-24"})));

    hgps::api::Report report;
    const auto configuration = hgps::api::load_configuration(config, {}, report);
    ASSERT_TRUE(configuration.has_value()) << report.to_string();
    EXPECT_TRUE(configuration->baseline_compat().is_set(hgps::api::CompatFlag::b24));
}
