// Which files this engine can write, and whether any fixture writes each of them.
//
// This test exists because of what the previous run found. The income-stratified CSVs had been
// written by every run of every income-enabled configuration for as long as this build has
// existed, and **no fixture produced one**: both synthetic packs were HLM, only the StaticLinear
// family gives a person an income category, so no test that ran a configuration could reach the
// code that fills those files. Forty-five of their columns were empty and nothing said so
// (docs/SUMMARY.md, docs/backlog.md item 2).
//
// The gap was not "a missing test for a function". It was that nothing anywhere enumerated what
// the engine puts on disk, so "is every output covered?" was not a question anything could be
// asked. `hgps::output::all_output_families()` is that enumeration, and this is the test that
// makes adding to it cost something: a new family with no fixture behind it fails here.
#include "output/result_writer.h"

#include "support/fixture_packs.h"
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include <map>
#include <set>
#include <string>

#include <gtest/gtest.h>

namespace {

using hgps::output::OutputFamily;
using hgps::output::output_family_of;
using hgps::output::output_family_name;

/// Every family produced by running every fixture pack once, with the packs that produced it.
std::map<OutputFamily, std::set<std::string>> families_across_the_packs() {
    std::map<OutputFamily, std::set<std::string>> found;

    for (const auto &pack : hgps::test::fixture_packs()) {
        const auto scratch =
            hgps::test::scratch_dir("output_families_" + pack.id);
        const auto outcome = hgps::test::run_simulation(pack.config(), scratch);
        EXPECT_TRUE(outcome.succeeded) << pack.id << ": " << outcome.report.to_string();

        for (const auto &path : outcome.all_paths) {
            found[output_family_of(path)].insert(pack.id);
        }
    }

    return found;
}

} // namespace

TEST(OutputFamilies, EveryFamilyTheEngineCanWriteIsProducedByAFixture) {
    const auto found = families_across_the_packs();

    for (const auto family : hgps::output::all_output_families()) {
        const auto entry = found.find(family);
        ASSERT_NE(found.end(), entry)
            << "no fixture pack produces the '" << output_family_name(family)
            << "' output family, so nothing in this suite ever reads one. Either a pack should "
               "produce it — see tools/gen-fixtures/model_pack.h — or it should not be in "
               "all_output_families().";
        EXPECT_FALSE(entry->second.empty());
    }
}

TEST(OutputFamilies, TheStratifiedFilesAreTheSecondPacksAndThereIsOnePerCategory) {
    // Named rather than merely counted: which pack covers the stratified families is the fact the
    // previous run got wrong by not asking. Under ThreadSanitizer only the first pack runs
    // (ADR 0046), and there the assertion is that the family is absent — the same statement from
    // the other side, and a check that the first pack has not quietly acquired an income model.
    const auto found = families_across_the_packs();
    const bool both_packs = hgps::test::fixture_packs().size() > 1;

    const auto stratum = found.find(OutputFamily::income_stratum);
    if (!both_packs) {
        EXPECT_EQ(found.end(), stratum)
            << "the first pack has no income model, so it cannot produce a stratum file";
        GTEST_SKIP() << "only one fixture pack runs under ThreadSanitizer";
    }

    ASSERT_NE(found.end(), stratum);
    EXPECT_EQ(std::set<std::string>{"synthetic-b"}, stratum->second);

    // Three categories, so three files, and every one of them opened whether or not anybody is in
    // it — which is this build's difference from the baseline, where a stratum file appears on
    // first use and a year with nobody in the stratum leaves a gap in its calendar.
    const auto scratch = hgps::test::scratch_dir("output_families_stratum_count");
    const auto &pack = hgps::test::fixture_packs().at(1);
    const auto outcome = hgps::test::run_simulation(pack.config(), scratch);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    std::size_t stratum_files = 0;
    for (const auto &path : outcome.all_paths) {
        if (output_family_of(path) == OutputFamily::income_stratum) {
            ++stratum_files;
        }
    }
    EXPECT_EQ(3U, stratum_files);
}

TEST(OutputFamilies, AFileIsClassifiedByItsName) {
    // The rule the harness and scripts/column-coverage.py apply to the baseline's output, checked
    // here against the names this writer actually produces, so the two sides group by the same
    // families.
    EXPECT_EQ(OutputFamily::result, output_family_of("results/result.csv"));
    EXPECT_EQ(OutputFamily::result, output_family_of("results/synthland_2026-01-02_B.csv"));
    EXPECT_EQ(OutputFamily::income_stratum, output_family_of("results/result_LowIncome.csv"));
    EXPECT_EQ(OutputFamily::income_stratum,
              output_family_of("results/result_UpperMiddleIncome.csv"));
    EXPECT_EQ(OutputFamily::metadata, output_family_of("results/result.json"));
    EXPECT_EQ(OutputFamily::manifest, output_family_of("results/result_manifest.json"));
    EXPECT_THROW((void)output_family_of("results/result.txt"), std::invalid_argument);
}
