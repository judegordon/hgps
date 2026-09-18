// The perturbation knob: the thing that makes the equivalence harness's failure test possible.
//
// It exists to produce deliberately wrong output, which is a dangerous thing to have in an engine, so
// what these tests mostly check is the safety rail: that it is off unless asked for, that a typo is
// refused rather than silently ignored, and that a run which used it says so in its manifest.
#include "engine/perturbation.h"

#include "support/fixture_packs.h"
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include "hgps/engine.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using hgps::engine::Perturbation;
using hgps::engine::PerturbationRule;

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

nlohmann::json read_json(const std::filesystem::path &path) {
    std::ifstream stream{path};
    return nlohmann::json::parse(stream);
}

/// The run-level half of these tests goes through both synthetic packs; the parsing half has no
/// configuration in it (tests/support/fixture_packs.h).
class PerturbedRun : public hgps::test::FixturePackTest {};

HGPS_TEST_EVERY_FIXTURE_PACK(PerturbedRun);

} // namespace

TEST(Perturbation, AnEmptySpecificationIsNoPerturbation) {
    std::string error;
    for (const char *text : {"", "   ", "\t"}) {
        const auto parsed = Perturbation::parse(text, error);
        ASSERT_TRUE(parsed.has_value()) << text << ": " << error;
        EXPECT_TRUE(parsed->empty());
    }
}

TEST(Perturbation, ParsesTheSpecificationTheHarnessUses) {
    std::string error;
    const auto parsed =
        Perturbation::parse("mean_bmi=scale:1.01;mean_energy=scale:1.05;emigrations=step:1", error);
    ASSERT_TRUE(parsed.has_value()) << error;
    ASSERT_EQ(3U, parsed->rules().size());

    EXPECT_EQ("mean_bmi", parsed->rules()[0].channel);
    EXPECT_EQ(PerturbationRule::Operation::scale, parsed->rules()[0].operation);
    EXPECT_DOUBLE_EQ(1.01, parsed->rules()[0].value);

    EXPECT_EQ("emigrations", parsed->rules()[2].channel);
    EXPECT_EQ(PerturbationRule::Operation::step, parsed->rules()[2].operation);
    EXPECT_DOUBLE_EQ(1.0, parsed->rules()[2].value);
}

TEST(Perturbation, ATypoIsRefusedRatherThanIgnored) {
    // The single worst outcome available here is a perturbation that silently does nothing: the test
    // that uses it asserts a *failure*, so an unperturbed run would make it pass for the wrong
    // reason and the harness would be declared able to detect something it cannot.
    for (const char *bad : {"mean_bmi", "mean_bmi=1.01", "mean_bmi=scale", "=scale:1.01",
                            "mean_bmi=multiply:1.01", "mean_bmi=scale:banana",
                            "mean_bmi=scale:1.01x", ";", "  ;  "}) {
        std::string error;
        const auto parsed = Perturbation::parse(bad, error);
        EXPECT_FALSE(parsed.has_value()) << "'" << bad << "' should not parse";
        EXPECT_FALSE(error.empty()) << bad;
    }
}

TEST(Perturbation, ScalingMultipliesEveryBandAndNothingElse) {
    hgps::model::ModelResult result{3};
    result.series.add_channels({"count", "mean_bmi", "mean_energy"});
    for (const auto gender : {hgps::core::Gender::male, hgps::core::Gender::female}) {
        result.series.at(gender, "count") = {10.0, 20.0, 30.0};
        result.series.at(gender, "mean_bmi") = {25.0, 26.0, 27.0};
        result.series.at(gender, "mean_energy") = {2000.0, 2100.0, 2200.0};
    }

    std::string error;
    auto perturbation = Perturbation::parse("mean_bmi=scale:1.01", error);
    ASSERT_TRUE(perturbation.has_value()) << error;
    perturbation->apply(result);

    for (const auto gender : {hgps::core::Gender::male, hgps::core::Gender::female}) {
        EXPECT_DOUBLE_EQ(25.0 * 1.01, result.series.at(gender, "mean_bmi").at(0));
        EXPECT_DOUBLE_EQ(27.0 * 1.01, result.series.at(gender, "mean_bmi").at(2));
        // Untouched, and that is the property the harness's "leaked" check depends on.
        EXPECT_DOUBLE_EQ(2000.0, result.series.at(gender, "mean_energy").at(0));
        EXPECT_DOUBLE_EQ(10.0, result.series.at(gender, "count").at(0));
    }
}

TEST(Perturbation, AStepMovesOneBandAndItIsTheFirstWithAnybodyInIt) {
    // One band, because the reduction sums a counted channel over the bands: one band is one lattice
    // step of the reduced series, which is what the lattice rule is about. And the *first occupied*
    // band, because an empty band is excluded from the reduction on both sides, so a step there would
    // be thrown away.
    hgps::model::ModelResult result{4};
    result.series.add_channels({"count", "emigrations"});
    for (const auto gender : {hgps::core::Gender::male, hgps::core::Gender::female}) {
        result.series.at(gender, "count") = {0.0, 0.0, 5.0, 7.0};
        result.series.at(gender, "emigrations") = {0.0, 0.0, 1.0, 2.0};
    }

    std::string error;
    auto perturbation = Perturbation::parse("emigrations=step:1", error);
    ASSERT_TRUE(perturbation.has_value()) << error;
    perturbation->apply(result);

    for (const auto gender : {hgps::core::Gender::male, hgps::core::Gender::female}) {
        const auto &values = result.series.at(gender, "emigrations");
        EXPECT_DOUBLE_EQ(0.0, values.at(0));
        EXPECT_DOUBLE_EQ(0.0, values.at(1));
        EXPECT_DOUBLE_EQ(2.0, values.at(2)) << "the first band with people in it";
        EXPECT_DOUBLE_EQ(2.0, values.at(3)) << "and no other";
    }
}

TEST(Perturbation, ARuleForAChannelTheOutputDoesNotHaveIsReported) {
    hgps::model::ModelResult result{2};
    result.series.add_channels({"count", "mean_bmi"});
    for (const auto gender : {hgps::core::Gender::male, hgps::core::Gender::female}) {
        result.series.at(gender, "count") = {1.0, 1.0};
        result.series.at(gender, "mean_bmi") = {25.0, 25.0};
    }

    std::string error;
    auto perturbation = Perturbation::parse("mean_bmi=scale:1.01;mean_nonsense=scale:2.0", error);
    ASSERT_TRUE(perturbation.has_value()) << error;
    perturbation->apply(result);

    const auto missed = perturbation->rules_that_never_fired();
    ASSERT_EQ(1U, missed.size());
    EXPECT_EQ("mean_nonsense", missed.front());
}

TEST_P(PerturbedRun, IsOffByDefaultAndTheManifestSaysSo) {
    const auto outcome = hgps::test::run_simulation(pack().config(), pack_scratch("perturb_off"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto manifest = read_json(outcome.manifest_path);
    ASSERT_TRUE(manifest.contains("perturbation"))
        << "every manifest answers 'was this perturbed?', including the ones that were not";
    EXPECT_TRUE(manifest.at("perturbation").is_null());
}

TEST_P(PerturbedRun, ChangesTheResultsAndTheManifestRecordsWhatItWas) {
    const auto clean =
        hgps::test::run_simulation(pack().config(), pack_scratch("perturb_clean"));
    ASSERT_TRUE(clean.succeeded) << clean.report.to_string();

    const auto dirty = hgps::test::run_simulation_perturbed(
        pack().config(), pack_scratch("perturb_dirty"),
        "mean_bmi=scale:1.01");
    ASSERT_TRUE(dirty.succeeded) << dirty.report.to_string();

    EXPECT_NE(read_file(clean.csv_path), read_file(dirty.csv_path));

    const auto manifest = read_json(dirty.manifest_path);
    EXPECT_EQ("mean_bmi=scale:1.01", manifest.at("perturbation").get<std::string>());
}

TEST_P(PerturbedRun, AChannelThatNeverExistedFailsTheRun) {
    // The run happens and the files are written, but it did not do what it was asked, so it is not a
    // success. That is what keeps a mistyped channel from producing a clean unperturbed run that the
    // harness's failure test would then report as "the harness cannot detect this".
    const auto outcome = hgps::test::run_simulation_perturbed(
        pack().config(), pack_scratch("perturb_typo"),
        "mean_bmi=scale:1.01;no_such_channel=scale:2.0");

    EXPECT_FALSE(outcome.succeeded);
    EXPECT_TRUE(outcome.report.has_errors()) << outcome.report.to_string();
    EXPECT_NE(std::string::npos, outcome.report.to_string().find("no_such_channel"));
}

TEST_P(PerturbedRun, AnUnparsableSpecificationIsRefusedBeforeAnythingRuns) {
    const auto outcome = hgps::test::run_simulation_perturbed(
        pack().config(), pack_scratch("perturb_unparsable"),
        "mean_bmi=multiply:2");

    EXPECT_FALSE(outcome.succeeded);
    EXPECT_TRUE(outcome.report.has_errors()) << outcome.report.to_string();
    EXPECT_EQ(0U, outcome.years_completed) << "nothing should have been simulated";
}
