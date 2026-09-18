// The test neither existing codebase has.
//
// docs/audit/03-baseline-determinism.md section 3: "there is no reproducibility test anywhere in
// the 471-test suite — no test runs a simulation twice and compares. Experiments A–F had to be
// constructed from outside. A new rewrite should ship that test."
//
// This is it. Determinism contract, docs/design.md section 4:
//
//   same config + same seed + same data + same binary => byte-identical CSV output, every run,
//   regardless of thread count.
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace {

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// The line at which two texts first differ, for a failure message that says where.
std::string first_difference(const std::string &left, const std::string &right) {
    std::istringstream left_stream{left};
    std::istringstream right_stream{right};

    std::string left_line;
    std::string right_line;
    std::size_t line = 0;

    while (true) {
        const bool has_left = static_cast<bool>(std::getline(left_stream, left_line));
        const bool has_right = static_cast<bool>(std::getline(right_stream, right_line));
        ++line;

        if (!has_left && !has_right) {
            return "no difference";
        }
        if (!has_left) {
            return fmt::format("line {}: first run ended, second has '{}'", line, right_line);
        }
        if (!has_right) {
            return fmt::format("line {}: second run ended, first has '{}'", line, left_line);
        }
        if (left_line != right_line) {
            return fmt::format("line {}:\n  first:  {}\n  second: {}", line, left_line,
                               right_line);
        }
    }
}

} // namespace

TEST(Reproducibility, TwoRunsOfTheSameConfigAreByteIdentical) {
    const auto first = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                  hgps::test::scratch_dir("repro_first"));
    ASSERT_TRUE(first.succeeded) << first.report.to_string();

    const auto second = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                   hgps::test::scratch_dir("repro_second"));
    ASSERT_TRUE(second.succeeded) << second.report.to_string();

    const auto left = read_file(first.csv_path);
    const auto right = read_file(second.csv_path);

    ASSERT_FALSE(left.empty());
    EXPECT_EQ(left, right) << "the two runs differ at " << first_difference(left, right);
}

TEST(Reproducibility, TheOutputIsIdenticalAtOneThreadAndAtManyThreads) {
    // The baseline is bit-identical across thread counts for a baseline-only run but not with an
    // intervention active, because its rows arrive in thread-completion order (audit B-01,
    // experiments D and F). Here the thread count only affects the RNG-free parallel sections,
    // whose reductions have a fixed block order (determinism clause D5).
    const auto single = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                   hgps::test::scratch_dir("repro_t1"), 1);
    ASSERT_TRUE(single.succeeded) << single.report.to_string();

    const auto many = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                 hgps::test::scratch_dir("repro_t8"), 8);
    ASSERT_TRUE(many.succeeded) << many.report.to_string();

    const auto left = read_file(single.csv_path);
    const auto right = read_file(many.csv_path);

    ASSERT_FALSE(left.empty());
    EXPECT_EQ(left, right) << "one thread and eight threads differ at "
                           << first_difference(left, right);
}

TEST(Reproducibility, WithAnInterventionActiveTheOutputIsStillByteIdentical) {
    // This is the case the baseline fails: three same-seed runs with an intervention produced
    // three different files, identical only after sorting.
    auto document = hgps::test::synthetic_config_document();
    document["running"]["interventions"]["active_type_id"] = "simple";

    const auto config = hgps::test::write_config_variant("repro_intervention_config", document);

    const auto first =
        hgps::test::run_simulation(config, hgps::test::scratch_dir("repro_int_first"));
    ASSERT_TRUE(first.succeeded) << first.report.to_string();

    const auto second =
        hgps::test::run_simulation(config, hgps::test::scratch_dir("repro_int_second"), 4);
    ASSERT_TRUE(second.succeeded) << second.report.to_string();

    const auto left = read_file(first.csv_path);
    const auto right = read_file(second.csv_path);

    ASSERT_FALSE(left.empty());
    EXPECT_EQ(left, right) << "the two runs differ at " << first_difference(left, right);
}

// The determinism contract, for each of the six interventions rather than only for `simple`.
// Three of them draw random numbers of their own — `dynamic_marketing`, `physical_activity` and
// `food_labelling` — so each is a place the contract could be broken independently, and each gets
// its own test so that a failure names the policy and so that no single test runs twenty-four
// simulations. That last part is not tidiness: twenty-four of them in one test took longer than
// CTest's timeout under ThreadSanitizer, which is the preset that most needs to run them.
//
// Four runs each: twice at one thread, to catch a run that depends on anything but the seed, and
// twice at four, to catch one that depends on the thread count.
namespace {

void expect_intervention_is_reproducible(const std::string &identifier,
                                         const nlohmann::json &extra) {
    auto document = hgps::test::synthetic_config_document();
    auto definition = document["running"]["interventions"]["types"]["simple"];
    for (const auto &member : extra.items()) {
        definition[member.key()] = member.value();
    }
    document["running"]["interventions"]["types"][identifier] = definition;
    document["running"]["interventions"]["active_type_id"] = identifier;

    const auto config = hgps::test::write_config_variant("repro_" + identifier + "_config",
                                                         document);

    std::vector<std::string> outputs;
    for (const auto &[label, threads] : std::vector<std::pair<std::string, std::size_t>>{
             {"one_a", 1}, {"one_b", 1}, {"many_a", 4}, {"many_b", 4}}) {
        const auto outcome = hgps::test::run_simulation(
            config, hgps::test::scratch_dir("repro_" + identifier + "_" + label), threads);
        ASSERT_TRUE(outcome.succeeded) << identifier << ": " << outcome.report.to_string();
        outputs.push_back(read_file(outcome.csv_path));
        ASSERT_FALSE(outputs.back().empty()) << identifier;
    }

    for (std::size_t i = 1; i < outputs.size(); ++i) {
        EXPECT_EQ(outputs[0], outputs[i])
            << identifier << ", run " << i << " differs at "
            << first_difference(outputs[0], outputs[i]);
    }
}

nlohmann::json intervention_definition(const std::string &identifier) {
    const std::vector<std::pair<std::string, nlohmann::json>> interventions{
        {"simple", nlohmann::json::object()},
        {"marketing",
         {{"impacts", {{{"risk_factor", "BMI"}, {"impact_value", -0.12}, {"from_age", 5},
                        {"to_age", 12}},
                       {{"risk_factor", "BMI"}, {"impact_value", -0.31}, {"from_age", 13},
                        {"to_age", 18}},
                       {{"risk_factor", "BMI"}, {"impact_value", -0.16}, {"from_age", 19},
                        {"to_age", nullptr}}}}}},
        {"dynamic_marketing",
         {{"dynamics", {0.4, 0.2, 0.3}},
          {"impacts", {{{"risk_factor", "BMI"}, {"impact_value", -0.12}, {"from_age", 5},
                        {"to_age", 12}},
                       {{"risk_factor", "BMI"}, {"impact_value", -0.31}, {"from_age", 13},
                        {"to_age", 18}},
                       {{"risk_factor", "BMI"}, {"impact_value", -0.16}, {"from_age", 19},
                        {"to_age", nullptr}}}}}},
        {"fiscal",
         {{"impact_type", "pessimist"},
          {"impacts", {{{"risk_factor", "Energy"}, {"impact_value", -0.017}, {"from_age", 5},
                        {"to_age", 9}},
                       {{"risk_factor", "Energy"}, {"impact_value", -0.018}, {"from_age", 10},
                        {"to_age", 17}},
                       {{"risk_factor", "Energy"}, {"impact_value", -0.019}, {"from_age", 18},
                        {"to_age", nullptr}}}}}},
        {"physical_activity",
         {{"coverage_rates", {0.6}},
          {"impacts", {{{"risk_factor", "PA"}, {"impact_value", 40.0}, {"from_age", 6},
                        {"to_age", 11}},
                       {{"risk_factor", "PA"}, {"impact_value", 20.0}, {"from_age", 12},
                        {"to_age", nullptr}}}}}},
        {"food_labelling",
         {{"coverage_rates", {0.3, 0.6}},
          {"coverage_cutoff_time", 2},
          {"child_cutoff_age", 18},
          {"coefficients", {0.1, 0.11, 0.12, 0.13}},
          {"adjustments", {{{"risk_factor", "Energy"}, {"value", 0.25}}}},
          {"impacts", {{{"risk_factor", "BMI"}, {"impact_value", -0.05}, {"from_age", 5},
                        {"to_age", nullptr}}}}}},
    };

    for (const auto &[name, extra] : interventions) {
        if (name == identifier) {
            return extra;
        }
    }
    ADD_FAILURE() << "no definition for intervention '" << identifier << "'";
    return nlohmann::json::object();
}

} // namespace

TEST(Reproducibility, SimpleIsByteIdenticalAtOneThreadAndAtManyThreadsTwice) {
    expect_intervention_is_reproducible("simple", intervention_definition("simple"));
}

TEST(Reproducibility, MarketingIsByteIdenticalAtOneThreadAndAtManyThreadsTwice) {
    expect_intervention_is_reproducible("marketing", intervention_definition("marketing"));
}

TEST(Reproducibility, DynamicMarketingIsByteIdenticalAtOneThreadAndAtManyThreadsTwice) {
    expect_intervention_is_reproducible("dynamic_marketing",
                                        intervention_definition("dynamic_marketing"));
}

TEST(Reproducibility, FiscalIsByteIdenticalAtOneThreadAndAtManyThreadsTwice) {
    expect_intervention_is_reproducible("fiscal", intervention_definition("fiscal"));
}

TEST(Reproducibility, PhysicalActivityIsByteIdenticalAtOneThreadAndAtManyThreadsTwice) {
    expect_intervention_is_reproducible("physical_activity",
                                        intervention_definition("physical_activity"));
}

TEST(Reproducibility, FoodLabellingIsByteIdenticalAtOneThreadAndAtManyThreadsTwice) {
    expect_intervention_is_reproducible("food_labelling",
                                        intervention_definition("food_labelling"));
}

TEST(Reproducibility, ADifferentSeedGivesDifferentOutput) {
    // The other half of the contract: reproducible must not mean insensitive.
    auto document = hgps::test::synthetic_config_document();
    document["running"]["seed"] = 987654321;

    const auto config = hgps::test::write_config_variant("repro_other_seed_config", document);

    const auto baseline = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                     hgps::test::scratch_dir("repro_seed_a"));
    ASSERT_TRUE(baseline.succeeded) << baseline.report.to_string();

    const auto other = hgps::test::run_simulation(config, hgps::test::scratch_dir("repro_seed_b"));
    ASSERT_TRUE(other.succeeded) << other.report.to_string();

    EXPECT_NE(read_file(baseline.csv_path), read_file(other.csv_path));
}

TEST(Reproducibility, TheMetadataRecordsTheSeedThatWasUsed) {
    // Baseline finding B-06: the results file records `seed().value_or(0)`, so an unseeded run
    // claims seed 0 and re-running with 0 does not reproduce it. Here the seed is required, and
    // every run's derived seed is recorded so a single run can be reproduced on its own.
    const auto outcome = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                    hgps::test::scratch_dir("repro_metadata"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    std::ifstream stream{outcome.json_path};
    const auto document = nlohmann::json::parse(stream);

    ASSERT_TRUE(document.contains("metadata"));
    const auto &metadata = document["metadata"];

    EXPECT_EQ(123456789U, metadata["seed"].get<std::uint32_t>());
    ASSERT_TRUE(metadata.contains("run_seeds"));
    EXPECT_EQ(1U, metadata["run_seeds"].size());
    EXPECT_EQ(64U, metadata["config_sha256"].get<std::string>().size());

    // The timestamp lives here, and nowhere in the CSV — which is what makes two runs of the
    // same config byte-comparable (audit N-15, N-16).
    ASSERT_TRUE(metadata.contains("started_utc"));
    const auto started = metadata["started_utc"].get<std::string>();
    EXPECT_EQ(std::string::npos, read_file(outcome.csv_path).find(started));
    EXPECT_EQ(std::string::npos, read_file(outcome.csv_path).find("utc"));
}
