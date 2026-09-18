// The population impact fraction, read from a real data store and applied to a real run.
//
// The table's own arithmetic is `tests/model/pif_test.cpp`. This is the other half: that the reader
// finds the tables where the index says they are, that a config asking for fractions the store does
// not have is refused rather than shrugged at, and — the one that matters — that the mechanism
// actually reduces incidence, in the intervention scenario and not in the baseline (ADR 0038).
#include "data/store.h"

#include "diagnostics/issue_report.h"
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <sstream>
#include <string>
#include <vector>

namespace {

using hgps::core::Gender;
using hgps::diag::IssueReport;

hgps::data::Store open_synthetic_store() {
    IssueReport report;
    auto store = hgps::data::Store::open(hgps::test::synthetic_data_dir(), report);
    EXPECT_TRUE(store.has_value()) << report.to_string();
    return std::move(*store);
}

hgps::core::DiseaseInfo disease(const hgps::data::Store &store, const char *code) {
    IssueReport report;
    const auto info = store.disease_info(hgps::core::Identifier{code}, report);
    EXPECT_TRUE(info.has_value()) << report.to_string();
    return *info;
}

hgps::core::Country country(const hgps::data::Store &store) {
    IssueReport report;
    const auto found = store.country("SYN", report);
    EXPECT_TRUE(found.has_value()) << report.to_string();
    return *found;
}

/// The reduced result CSV as {(scenario, year, variable) -> count-weighted total}, for the channels a
/// PIF is expected to move.
std::map<std::tuple<std::string, int, std::string>, double>
incidence_totals(const std::filesystem::path &csv) {
    std::ifstream stream{csv};
    std::string header;
    std::getline(stream, header);

    std::vector<std::string> columns;
    for (std::stringstream parts{header}; parts.good();) {
        std::string name;
        std::getline(parts, name, ',');
        columns.push_back(name);
    }

    std::map<std::tuple<std::string, int, std::string>, double> totals;
    std::string line;
    while (std::getline(stream, line)) {
        std::vector<std::string> fields;
        for (std::stringstream parts{line}; parts.good();) {
            std::string value;
            std::getline(parts, value, ',');
            fields.push_back(value);
        }
        if (fields.size() != columns.size()) {
            continue;
        }

        const auto scenario = fields[0];
        const auto year = std::stoi(fields[2]);
        const auto count = std::stod(fields[5]);
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (columns[i].starts_with("incidence_")) {
                totals[{scenario, year, columns[i]}] += count * std::stod(fields[i]);
            }
        }
    }
    return totals;
}

} // namespace

TEST(PopulationImpactFractionData, IsReadFromWhereTheIndexSaysItIs) {
    const auto store = open_synthetic_store();
    IssueReport report;

    const auto rows = store.population_impact_fraction(disease(store, "asthma"), country(store),
                                                       "Smoking", "Scenario1", report);
    ASSERT_TRUE(rows.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors());
    EXPECT_FALSE(rows->empty());

    // Every cell of the declared range, once: 2 sexes x (max_age + 1) ages x 8 years.
    std::set<std::tuple<int, int, int>> cells;
    for (const auto &row : *rows) {
        cells.insert({row.gender == Gender::male ? 0 : 1, row.age, row.years_since_intervention});
    }
    EXPECT_EQ(rows->size(), cells.size()) << "a cell appears twice";
}

TEST(PopulationImpactFractionData, TheScenarioNameSelectsADifferentTable) {
    // Otherwise the scenario key would be decoration, and a study comparing Scenario1 with Scenario2
    // would be comparing a table with itself.
    const auto store = open_synthetic_store();
    IssueReport report;

    const auto first = store.population_impact_fraction(disease(store, "asthma"), country(store),
                                                        "Smoking", "Scenario1", report);
    const auto second = store.population_impact_fraction(disease(store, "asthma"), country(store),
                                                         "Smoking", "Scenario2", report);
    ASSERT_TRUE(first.has_value() && second.has_value()) << report.to_string();
    ASSERT_EQ(first->size(), second->size());

    bool any_different = false;
    for (std::size_t i = 0; i < first->size(); ++i) {
        any_different |= (*first)[i].value != (*second)[i].value;
    }
    EXPECT_TRUE(any_different);
}

TEST(PopulationImpactFractionData, ARiskFactorTheStoreDoesNotHaveIsAnErrorNamingWhatItDoesHave) {
    // Upstream warns — silently, unless the run is verbose — and returns nothing, so a config naming a
    // risk factor the store does not have for a disease runs with no policy applied to that disease
    // and reports success. `KevinHall_PIF`'s own legacy `config.json` does exactly that for four of
    // the fifteen diseases it selects (deviation B-26, docs/examples.md).
    const auto store = open_synthetic_store();
    IssueReport report;

    const auto rows = store.population_impact_fraction(disease(store, "asthma"), country(store),
                                                       "Joint", "Scenario1", report);
    EXPECT_FALSE(rows.has_value());
    ASSERT_TRUE(report.has_errors()) << report.to_string();

    const auto text = report.to_string();
    EXPECT_NE(std::string::npos, text.find("Joint"));
    EXPECT_NE(std::string::npos, text.find("Scenario1"));
    // And what it does have, because that usually answers the question in the same breath.
    EXPECT_NE(std::string::npos, text.find("Smoking"));
}

TEST(PopulationImpactFractionData, TheSexColumnIsZeroForMaleAndOneForFemale) {
    // The published schema says the opposite — "Gender code: 0 = female, 1 = male" — and it is wrong:
    // upstream's own loader reads `csv_gender == 0 ? male : female`, and the data agrees. The decisive
    // case is in the real pack rather than here: `cervicalcancer`, a female-only disease, has a table
    // that is non-zero only at Gender=1 and identically zero at Gender=0. That pack is 5.4 MB fetched
    // from a release URL and is not vendored, so the evidence lives in docs/deviations.md (B-27) with
    // the command that reproduces it, and what is pinned here is that the reader implements the
    // encoding the data has.
    //
    // The synthetic pack's generator writes larger fractions for sex 0 than for sex 1, so the mapping
    // is visible from the values.
    const auto store = open_synthetic_store();
    IssueReport report;
    const auto rows = store.population_impact_fraction(disease(store, "asthma"), country(store),
                                                       "Smoking", "Scenario1", report);
    ASSERT_TRUE(rows.has_value()) << report.to_string();

    double male = -1.0;
    double female = -1.0;
    for (const auto &row : *rows) {
        if (row.age == 45 && row.years_since_intervention == 7) {
            (row.gender == Gender::male ? male : female) = row.value;
        }
    }
    ASSERT_GE(male, 0.0);
    ASSERT_GE(female, 0.0);
    EXPECT_GT(male, female) << "sex 0 in the file is male, so it must be the larger of the pack's two";
}

TEST(PopulationImpactFractionData, AScenarioTheStoreDoesNotHaveIsAnError) {
    const auto store = open_synthetic_store();
    IssueReport report;
    EXPECT_FALSE(store.population_impact_fraction(disease(store, "asthma"), country(store),
                                                  "Smoking", "Scenario9", report)
                     .has_value());
    EXPECT_TRUE(report.has_errors());
}

TEST(PopulationImpactFractionRun, ReducesIncidenceInTheInterventionScenarioOnly) {
    // The whole mechanism, end to end, through the public API: a run with fractions against the same
    // run without them. The baseline scenario must be untouched — a PIF is a policy, not a correction —
    // and the intervention scenario's incidence must fall.
    auto without = hgps::test::synthetic_config_document();
    without["running"]["interventions"]["active_type_id"] = "simple";
    const auto plain = hgps::test::write_config_variant("pif_off", without);

    auto with = without;
    with["population_impact_fraction"] = {{"enabled", true},
                                          {"risk_factor", "Smoking"},
                                          {"scenario", "Scenario2"}};
    const auto policy = hgps::test::write_config_variant("pif_on", with);

    const auto off = hgps::test::run_simulation(plain, hgps::test::scratch_dir("pif_off_out"));
    ASSERT_TRUE(off.succeeded) << off.report.to_string();
    const auto on = hgps::test::run_simulation(policy, hgps::test::scratch_dir("pif_on_out"));
    ASSERT_TRUE(on.succeeded) << on.report.to_string();

    const auto before = incidence_totals(off.csv_path);
    const auto after = incidence_totals(on.csv_path);
    ASSERT_FALSE(before.empty());
    ASSERT_EQ(before.size(), after.size());

    std::size_t baseline_moved = 0;
    std::size_t intervention_fell = 0;
    std::size_t intervention_rose = 0;

    for (const auto &[key, value] : before) {
        const auto &[scenario, year, variable] = key;
        const auto other = after.at(key);
        if (scenario == "Baseline") {
            baseline_moved += (other != value) ? 1 : 0;
        } else if (other < value) {
            ++intervention_fell;
        } else if (other > value) {
            ++intervention_rose;
        }
    }

    EXPECT_EQ(0U, baseline_moved)
        << "the baseline scenario must be identical: a population impact fraction is a policy, and "
           "applying it to the future it is measured against would make the comparison meaningless";
    EXPECT_GT(intervention_fell, 0U) << "no incidence fell, so nothing was applied";
    EXPECT_GT(intervention_fell, intervention_rose)
        << "incidence should fall on balance: the fractions multiply the probability by (1 - PIF)";
}

TEST(PopulationImpactFractionRun, IsDeterministic) {
    auto document = hgps::test::synthetic_config_document();
    document["running"]["interventions"]["active_type_id"] = "simple";
    document["population_impact_fraction"] = {{"enabled", true},
                                              {"risk_factor", "Smoking"},
                                              {"scenario", "Scenario1"}};
    const auto config = hgps::test::write_config_variant("pif_repeat", document);

    const auto first = hgps::test::run_simulation(config, hgps::test::scratch_dir("pif_repeat_a"));
    const auto second = hgps::test::run_simulation(config, hgps::test::scratch_dir("pif_repeat_b"), 4);
    ASSERT_TRUE(first.succeeded) << first.report.to_string();
    ASSERT_TRUE(second.succeeded) << second.report.to_string();

    const auto read = [](const std::filesystem::path &path) {
        std::ifstream stream{path, std::ios::binary};
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    };
    EXPECT_EQ(read(first.csv_path), read(second.csv_path))
        << "a run with a population impact fraction is as reproducible as one without, at any thread "
           "count";
}

TEST(PopulationImpactFractionRun, AMissingRiskFactorStopsTheRunBeforeItStarts) {
    auto document = hgps::test::synthetic_config_document();
    document["running"]["interventions"]["active_type_id"] = "simple";
    document["population_impact_fraction"] = {{"enabled", true},
                                              {"risk_factor", "Joint"},
                                              {"scenario", "Scenario1"}};
    const auto config = hgps::test::write_config_variant("pif_missing", document);

    const auto outcome = hgps::test::run_simulation(config, hgps::test::scratch_dir("pif_missing_out"));
    EXPECT_FALSE(outcome.succeeded);
    EXPECT_TRUE(outcome.report.has_errors()) << outcome.report.to_string();
    EXPECT_EQ(0U, outcome.years_completed);
}
