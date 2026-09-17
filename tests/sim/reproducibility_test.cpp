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
