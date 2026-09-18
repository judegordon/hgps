// Ports the intent of the baseline's Simulation.Test.cpp (suite TestSimulation, 25 tests, 2 of
// them skipped on the missing FINCH fixture) and Scenario.Test.cpp (ScenarioTest, 23), plus
// Channel.Test.cpp (ChannelTest, 9) whose SyncChannel is replaced by the journal (ADR 0009).
//
// The baseline's simulation tests drive its modules through a hand-built RuntimeContext; these
// drive the real pipeline end to end on the synthetic pack, which exercises the same behaviour
// and also the wiring between the modules.
#include "sim/engine.h"
#include "sim/scenario.h"

#include "config/loader.h"
#include "diagnostics/internal_error.h"
#include "support/fixture_packs.h"
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Row {
    std::string source;
    int run{};
    int time{};
    std::string gender;
    int index{};
    std::map<std::string, double> values;
};

std::vector<Row> read_rows(const std::filesystem::path &path) {
    std::ifstream stream{path};
    std::string line;

    std::vector<std::string> headers;
    if (std::getline(stream, line)) {
        std::stringstream header_stream{line};
        std::string field;
        while (std::getline(header_stream, field, ',')) {
            headers.push_back(field);
        }
    }

    std::vector<Row> rows;
    while (std::getline(stream, line)) {
        std::stringstream row_stream{line};
        std::string field;
        std::vector<std::string> fields;
        while (std::getline(row_stream, field, ',')) {
            fields.push_back(field);
        }
        if (fields.size() != headers.size()) {
            continue;
        }

        Row row;
        row.source = fields[0];
        row.run = std::stoi(fields[1]);
        row.time = std::stoi(fields[2]);
        row.gender = fields[3];
        row.index = std::stoi(fields[4]);
        for (std::size_t i = 5; i < fields.size(); ++i) {
            row.values[headers[i]] = std::stod(fields[i]);
        }
        rows.push_back(std::move(row));
    }

    return rows;
}

double total(const std::vector<Row> &rows, const std::string &channel, int time,
             const std::string &source) {
    double sum = 0.0;
    for (const auto &row : rows) {
        if (row.time == time && row.source == source) {
            sum += row.values.at(channel);
        }
    }
    return sum;
}

/// The simulation is exercised against both synthetic packs, so nothing here may assert a horizon,
/// a scenario set, a disease list or an age range — every one of those differs between them, and
/// every one of them was a literal in this file (tests/support/fixture_packs.h).
class TestSimulation : public hgps::test::FixturePackTest {
  protected:
    nlohmann::json document() const { return hgps::test::config_document(pack()); }

    int start_year() const { return document()["running"]["start_time"].get<int>(); }
    int stop_year() const { return document()["running"]["stop_time"].get<int>(); }
    int top_age() const { return document()["inputs"]["settings"]["age_range"][1].get<int>(); }

    std::vector<std::string> diseases() const {
        return document()["running"]["diseases"].get<std::vector<std::string>>();
    }

    /// @brief The scenarios the pack's own configuration produces, in order.
    std::vector<std::string> scenarios() const {
        if (document()["running"]["interventions"]["active_type_id"].is_null()) {
            return {"Baseline"};
        }
        return {"Baseline", "Intervention"};
    }
};

HGPS_TEST_EVERY_FIXTURE_PACK(TestSimulation);

} // namespace

TEST_P(TestSimulation, RunsTheWholeHorizonAndWritesEveryYear) {
    const auto outcome = hgps::test::run_simulation(pack().config(), pack_scratch("sim_horizon"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto rows = read_rows(outcome.csv_path);
    ASSERT_FALSE(rows.empty());

    std::set<int> years;
    std::set<std::string> sources;
    for (const auto &row : rows) {
        years.insert(row.time);
        sources.insert(row.source);
    }

    // Every year of the pack's own horizon, and every scenario its own configuration produces.
    std::set<int> expected_years;
    for (int year = start_year(); year <= stop_year(); ++year) {
        expected_years.insert(year);
    }
    EXPECT_EQ(expected_years, years);

    const auto expected_scenarios = scenarios();
    EXPECT_EQ(std::set<std::string>(expected_scenarios.begin(), expected_scenarios.end()), sources);

    // Two sexes for every age in the configured range, for every year of every scenario.
    const auto ages = static_cast<std::size_t>(top_age()) + 1U;
    EXPECT_EQ(expected_years.size() * expected_scenarios.size() * 2U * ages, rows.size());
}

TEST_P(TestSimulation, AChannelExistsOnlyWhenAModelActuallyAssignsIt) {
    // project_requirements defaults switch income and physical activity on for every config,
    // including this one, whose HLM models assign neither. Emitting the channels anyway put six
    // columns of zeros in the reference example's output; the baseline instead decides by
    // sampling the first 1,000 people, so its column set depends on the cohort's contents.
    // Neither is right: the channel exists when the project asks for the dimension *and* a
    // loaded model gives it to people.
    auto config_doc = document();
    config_doc["project_requirements"]["income"]["enabled"] = true;
    config_doc["project_requirements"]["physical_activity"]["enabled"] = true;
    // region and ethnicity are not switched on here: the run refuses them outright without the
    // prevalence data, which is a different rule with its own test.
    const auto config = hgps::test::write_config_variant(pack(), "sim_channels_config", config_doc);

    const auto outcome = hgps::test::run_simulation(config, pack_scratch("sim_channels"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    std::ifstream stream{outcome.csv_path};
    ASSERT_TRUE(stream.good());
    std::string header;
    ASSERT_TRUE(std::getline(stream, header));

    for (const auto *absent : {"mean_income_category", "mean_income", "mean_physical_activity"}) {
        EXPECT_EQ(std::string::npos, header.find(absent))
            << absent << " should not be a column: no loaded model assigns it";
    }

    // What the models do assign is still there.
    for (const auto *present : {"mean_age", "mean_bmi", "mean_energy", "mean_yll"}) {
        EXPECT_NE(std::string::npos, header.find(present)) << present;
    }
}

TEST_P(TestSimulation, AChannelThatIsBothADeclaredFactorAndAMemberIsCountedOnce) {
    // The generalised form of the baseline's
    // AnalysisModuleDoesNotDoubleCountIncomeFieldsWhenMapped, and the regression test for the bug
    // the equivalence harness found: `gender` is declared as a level-0 risk factor, so it is in
    // the mapping, but a person carries it in `person.gender` rather than in `risk_factors`. The
    // sum came from the explicit accumulation and was then divided by the head count twice, once
    // in the demographic list and once in the mapping walk, so every male band reported 1/count.
    //
    // The property that catches the whole class: for a channel whose value is the same for
    // everyone in a band, the band's mean is that value.
    const auto outcome =
        hgps::test::run_simulation(pack().config(), pack_scratch("sim_no_double"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto rows = read_rows(outcome.csv_path);
    ASSERT_FALSE(rows.empty());

    std::size_t checked = 0;
    for (const auto &row : rows) {
        if (row.values.at("count") <= 0.0) {
            continue;
        }
        ++checked;

        // Everyone in a male band is male, so mean_gender is 1; everyone in a female band is
        // female, so it is 0. Either way the spread is zero.
        const double expected_gender = row.gender == "male" ? 1.0 : 0.0;
        EXPECT_DOUBLE_EQ(expected_gender, row.values.at("mean_gender"));
        EXPECT_DOUBLE_EQ(0.0, row.values.at("std_gender"));

        // And age, which is the row's own key.
        EXPECT_DOUBLE_EQ(static_cast<double>(row.index), row.values.at("mean_age"));
        EXPECT_DOUBLE_EQ(0.0, row.values.at("std_age"));
    }
    EXPECT_GT(checked, 100U) << "the fixture should have populated bands to check";
}

TEST_P(TestSimulation, RowsAreInSourceRunTimeGenderIndexOrder) {
    // Determinism clause D10 and ADR 0020: the row order is the output contract, and the
    // baseline's is thread-completion order (audit B-01).
    auto config_doc = document();
    config_doc["running"]["interventions"]["active_type_id"] = "simple";
    const auto config = hgps::test::write_config_variant(pack(), "sim_order_config", config_doc);

    const auto outcome = hgps::test::run_simulation(config, pack_scratch("sim_order"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto rows = read_rows(outcome.csv_path);
    ASSERT_FALSE(rows.empty());

    // Baseline first, then intervention; within each, ascending year; within each year,
    // ascending index with male before female.
    std::vector<std::string> sources;
    for (const auto &row : rows) {
        if (sources.empty() || sources.back() != row.source) {
            sources.push_back(row.source);
        }
    }
    EXPECT_EQ(std::vector<std::string>({"Baseline", "Intervention"}), sources);

    for (std::size_t i = 1; i < rows.size(); ++i) {
        const auto &previous = rows[i - 1];
        const auto &current = rows[i];
        if (previous.source != current.source) {
            continue;
        }
        if (previous.time != current.time) {
            EXPECT_LT(previous.time, current.time);
            continue;
        }
        if (previous.index != current.index) {
            EXPECT_LT(previous.index, current.index);
            continue;
        }
        EXPECT_EQ("male", previous.gender);
        EXPECT_EQ("female", current.gender);
    }
}

TEST_P(TestSimulation, ThePopulationAgesAndPeopleDieAndAreBorn) {
    const auto outcome =
        hgps::test::run_simulation(pack().config(), pack_scratch("sim_lifecycle"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto rows = read_rows(outcome.csv_path);

    // Nobody dies in the first year: the analysis runs before any year has elapsed.
    EXPECT_DOUBLE_EQ(0.0, total(rows, "deaths", start_year(), "Baseline"));

    // And people do die later.
    EXPECT_GT(total(rows, "deaths", start_year() + 2, "Baseline"), 0.0);

    // Newborns appear: the count at age 0 is positive in a later year.
    double newborns = 0.0;
    for (const auto &row : rows) {
        if (row.time == start_year() + 3 && row.index == 0) {
            newborns += row.values.at("count");
        }
    }
    EXPECT_GT(newborns, 0.0);

    // The cohort does not collapse or explode.
    const auto first_year = total(rows, "count", start_year(), "Baseline");
    const auto last_year = total(rows, "count", stop_year(), "Baseline");
    EXPECT_GT(last_year, first_year * 0.8);
    EXPECT_LT(last_year, first_year * 1.2);
}

TEST_P(TestSimulation, TheImmigrationShortfallIsReported) {
    // Deviation B-21. Immigration into an (age, sex) band clones somebody already in it, so an
    // empty band cannot be filled and the cohort falls short of the demographic projection. The
    // baseline does the same and says nothing; here every year's metrics carry the shortfall, so
    // a run reports its own divergence from the projection instead of leaving it to be found by
    // comparison. The synthetic pack's population table stops at the configured top age, so its
    // top band empties every year and the shortfall is reliably non-zero.
    const auto outcome =
        hgps::test::run_simulation(pack().config(), pack_scratch("sim_shortfall"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    std::ifstream stream{outcome.json_path};
    const auto results_json = nlohmann::json::parse(stream);
    ASSERT_TRUE(results_json.contains("results"));

    int years_with_the_metric = 0;
    double worst_shortfall = 0.0;
    const int first_year = start_year();
    for (const auto &entry : results_json["results"]) {
        if (entry["time"].get<int>() == first_year) {
            // The first year is the initial cohort, reported before any year has elapsed: there
            // is no migration step and so nothing to report about one.
            EXPECT_FALSE(entry.contains("metrics")) << entry.dump();
            continue;
        }

        ASSERT_TRUE(entry.contains("metrics")) << entry.dump();
        const auto &metrics = entry["metrics"];

        // Present every year, including the years with nothing to report, so the series is
        // complete rather than appearing only when something goes wrong.
        ASSERT_TRUE(metrics.contains("ImmigrationShortfallPeople")) << metrics.dump();
        ASSERT_TRUE(metrics.contains("ImmigrationShortfallBands")) << metrics.dump();
        ++years_with_the_metric;

        const double people = metrics["ImmigrationShortfallPeople"].get<double>();
        const double bands = metrics["ImmigrationShortfallBands"].get<double>();
        EXPECT_GE(people, 0.0);
        EXPECT_GE(bands, 0.0);
        // A shortfall needs a band to have fallen short, and a band that falls short is short by
        // at least one person.
        EXPECT_EQ(bands == 0.0, people == 0.0);
        EXPECT_GE(people, bands);
        worst_shortfall = std::max(worst_shortfall, people);
    }

    // The first year of each scenario has no migration step, so every other year reports.
    const auto reporting_years = stop_year() - start_year();
    EXPECT_EQ(reporting_years * static_cast<int>(scenarios().size()), years_with_the_metric);
    EXPECT_GT(worst_shortfall, 0.0);
}

TEST_P(TestSimulation, RiskFactorsAreCalibratedTowardsTheirExpectedMeans) {
    const auto outcome =
        hgps::test::run_simulation(pack().config(), pack_scratch("sim_calibration"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto rows = read_rows(outcome.csv_path);

    // The synthetic FactorsMean tables are 1500 + 900(1 - exp(-0.09 age)) for energy and
    // 15 + 11(1 - exp(-0.07 age)) for BMI, with females at 94% of those. The adjustment shifts
    // the simulated mean onto the expected one, so a mid-range age should land close.
    for (const auto &row : rows) {
        if (row.time != start_year() || row.index != 30 || row.gender != "male") {
            continue;
        }

        const double expected_energy = 1500.0 + 900.0 * (1.0 - std::exp(-0.09 * 30));
        const double expected_bmi = 15.0 + 11.0 * (1.0 - std::exp(-0.07 * 30));

        EXPECT_NEAR(expected_energy, row.values.at("mean_energy"), expected_energy * 0.05);
        EXPECT_NEAR(expected_bmi, row.values.at("mean_bmi"), expected_bmi * 0.05);
    }
}

TEST_P(TestSimulation, DiseasesAppearAndTheBurdenIsReported) {
    const auto outcome =
        hgps::test::run_simulation(pack().config(), pack_scratch("sim_disease"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto rows = read_rows(outcome.csv_path);

    // Prevalence is a share in [0, 1] per age, and somebody has each disease.
    for (const auto &disease : diseases()) {
        double prevalence = 0.0;
        for (const auto &row : rows) {
            const auto value = row.values.at("prevalence_" + disease);
            EXPECT_GE(value, 0.0) << disease;
            EXPECT_LE(value, 1.0) << disease;
            prevalence += value;
        }
        EXPECT_GT(prevalence, 0.0) << disease;
    }

    // The burden channels are populated and finite.
    for (const auto &row : rows) {
        EXPECT_GE(row.values.at("mean_yld"), 0.0);
        EXPECT_GE(row.values.at("mean_daly"), row.values.at("mean_yld") - 1e-9);
    }
}

TEST_P(TestSimulation, WeightCategoriesPartitionThePopulation) {
    const auto outcome =
        hgps::test::run_simulation(pack().config(), pack_scratch("sim_weight"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    for (const auto &row : read_rows(outcome.csv_path)) {
        const auto normal = row.values.at("normal_weight");
        const auto over = row.values.at("over_weight");
        const auto obese = row.values.at("obese_weight");
        const auto above = row.values.at("above_weight");
        const auto count = row.values.at("count");

        EXPECT_DOUBLE_EQ(over + obese, above);
        EXPECT_NEAR(normal + over + obese, count, 1e-9);
    }
}

TEST(ScenarioTest, TheBaselineScenarioLeavesValuesAlone) {
    hgps::rng::RandomSource random{1U};
    hgps::model::Person person{hgps::core::Gender::male, 1};
    person.age = 40;

    hgps::sim::BaselineScenario scenario;

    EXPECT_EQ(hgps::sim::ScenarioType::baseline, scenario.type());
    EXPECT_EQ("Baseline", scenario.name());
    EXPECT_DOUBLE_EQ(24.5, scenario.apply(random, person, 2025, hgps::core::Identifier{"bmi"},
                                          24.5));
    EXPECT_EQ(0U, random.draw_count()) << "the baseline scenario must not consume the stream";
}

TEST(ScenarioTest, TheSimplePolicyAppliesOnlyInItsActivePeriodAndAgeBand) {
    hgps::config::InterventionSpec definition;
    definition.identifier = "simple";
    definition.active_period.start_time = 2022;
    definition.active_period.finish_time = 2024;
    definition.impact_type = "absolute";
    definition.impacts.push_back(hgps::config::PolicyImpact{
        .risk_factor = "BMI", .impact_value = -1.5, .from_age = 18, .to_age = 65});

    hgps::sim::SimplePolicyScenario scenario{definition};
    hgps::rng::RandomSource random{1U};

    hgps::model::Person adult{hgps::core::Gender::male, 1};
    adult.age = 40;
    hgps::model::Person child{hgps::core::Gender::female, 2};
    child.age = 10;

    const auto bmi = hgps::core::Identifier{"bmi"};

    EXPECT_EQ(hgps::sim::ScenarioType::intervention, scenario.type());
    EXPECT_EQ("Intervention", scenario.name());

    // Inside the period and the age band.
    EXPECT_DOUBLE_EQ(23.0, scenario.apply(random, adult, 2023, bmi, 24.5));

    // Before and after the period.
    EXPECT_DOUBLE_EQ(24.5, scenario.apply(random, adult, 2021, bmi, 24.5));
    EXPECT_DOUBLE_EQ(24.5, scenario.apply(random, adult, 2025, bmi, 24.5));

    // Outside the age band.
    EXPECT_DOUBLE_EQ(18.0, scenario.apply(random, child, 2023, bmi, 18.0));

    // A factor the policy does not name.
    EXPECT_DOUBLE_EQ(2000.0,
                     scenario.apply(random, adult, 2023, hgps::core::Identifier{"energy"}, 2000.0));

    EXPECT_EQ(0U, random.draw_count()) << "a flat shift needs no randomness";
}

TEST(ScenarioTest, AnOpenEndedPolicyNeverStops) {
    hgps::config::InterventionSpec definition;
    definition.identifier = "simple";
    definition.active_period.start_time = 2022;
    definition.active_period.finish_time = std::nullopt;
    definition.impacts.push_back(hgps::config::PolicyImpact{
        .risk_factor = "BMI", .impact_value = -1.0, .from_age = 0, .to_age = std::nullopt});

    hgps::sim::SimplePolicyScenario scenario{definition};
    hgps::rng::RandomSource random{1U};

    hgps::model::Person person{hgps::core::Gender::male, 1};
    person.age = 80;

    const auto bmi = hgps::core::Identifier{"bmi"};
    EXPECT_DOUBLE_EQ(24.5, scenario.apply(random, person, 2021, bmi, 24.5));
    EXPECT_DOUBLE_EQ(23.5, scenario.apply(random, person, 2099, bmi, 24.5));
}

TEST(ScenarioTest, ARelativeImpactScales) {
    hgps::config::InterventionSpec definition;
    definition.identifier = "simple";
    definition.active_period.start_time = 2000;
    definition.impact_type = "relative";
    definition.impacts.push_back(hgps::config::PolicyImpact{
        .risk_factor = "Energy", .impact_value = -0.1, .from_age = 0, .to_age = std::nullopt});

    hgps::sim::SimplePolicyScenario scenario{definition};
    hgps::rng::RandomSource random{1U};

    hgps::model::Person person{hgps::core::Gender::male, 1};
    person.age = 30;

    EXPECT_DOUBLE_EQ(1800.0, scenario.apply(random, person, 2010,
                                            hgps::core::Identifier{"energy"}, 2000.0));
}

TEST(ScenarioTest, OnlyTheImplementedInterventionCanBeBuilt) {
    hgps::config::InterventionSpec definition;
    definition.identifier = "simple";
    definition.active_period.start_time = 2022;
    definition.impacts.push_back(hgps::config::PolicyImpact{
        .risk_factor = "BMI", .impact_value = -1.0, .from_age = 0, .to_age = std::nullopt});

    EXPECT_NE(nullptr, hgps::sim::create_intervention_scenario(definition));

    definition.identifier = "marketing";
    EXPECT_THROW(hgps::sim::create_intervention_scenario(definition),
                 hgps::diag::InternalError);
}

TEST_P(TestSimulation, AnActiveInterventionLowersTheFactorItTargets) {
    auto config_doc = document();
    config_doc["running"]["interventions"]["active_type_id"] = "simple";
    const auto config = hgps::test::write_config_variant(pack(), "sim_effect_config", config_doc);

    const auto outcome = hgps::test::run_simulation(config, pack_scratch("sim_effect"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto rows = read_rows(outcome.csv_path);

    // `simple` takes 1.0 off BMI from its own start year onwards, so the intervention's mean BMI
    // in the last year must be below the baseline's. In the run's first year the two futures are
    // identical, because they share the run seed and the journal and the policy has not run.
    const auto baseline_before = total(rows, "mean_bmi", start_year(), "Baseline");
    const auto intervention_before = total(rows, "mean_bmi", start_year(), "Intervention");
    EXPECT_DOUBLE_EQ(baseline_before, intervention_before)
        << "the two futures must be identical before the policy starts";

    const auto baseline_after = total(rows, "mean_bmi", stop_year(), "Baseline");
    const auto intervention_after = total(rows, "mean_bmi", stop_year(), "Intervention");
    EXPECT_LT(intervention_after, baseline_after);
}

TEST(ScenarioJournalTest, RecordsAndReplaysMigrationInYearOrder) {
    hgps::sim::ScenarioJournal journal;

    hgps::sim::MigrationEntry entry;
    entry.net_by_age_gender[30][hgps::core::Gender::male] = 5;
    entry.net_by_age_gender[30][hgps::core::Gender::female] = -3;

    EXPECT_FALSE(journal.contains_migration(1, 2010));
    journal.record_migration(1, 2010, entry);
    EXPECT_TRUE(journal.contains_migration(1, 2010));

    const auto &replayed = journal.replay_migration(1, 2010);
    EXPECT_EQ(5, replayed.net_by_age_gender.at(30).at(hgps::core::Gender::male));
    EXPECT_EQ(-3, replayed.net_by_age_gender.at(30).at(hgps::core::Gender::female));

    // Recording a year twice means the engine has lost track of where it is.
    EXPECT_THROW(journal.record_migration(1, 2010, entry), hgps::diag::InternalError);

    // Replaying a year that was never recorded means the two scenarios disagree about the
    // horizon — which is exactly the condition the baseline's channel timeout reports as a
    // timeout after 15 seconds.
    EXPECT_THROW(journal.replay_migration(1, 2011), hgps::diag::InternalError);
    EXPECT_THROW(journal.replay_migration(2, 2010), hgps::diag::InternalError);
}

TEST(ScenarioJournalTest, ReplaysAdjustmentsInRecordingOrder) {
    hgps::sim::ScenarioJournal journal;

    hgps::sim::AdjustmentTable first;
    first.emplace(hgps::core::Gender::male, hgps::core::Identifier{"bmi"},
                  std::vector<double>{1.0});
    hgps::sim::AdjustmentTable second;
    second.emplace(hgps::core::Gender::male, hgps::core::Identifier{"bmi"},
                   std::vector<double>{2.0});

    journal.push_adjustment(1, 2010, first);
    journal.push_adjustment(1, 2010, second);

    EXPECT_DOUBLE_EQ(1.0, journal.pop_adjustment(1, 2010)
                              .at(hgps::core::Gender::male, hgps::core::Identifier{"bmi"})
                              .front());
    EXPECT_DOUBLE_EQ(2.0, journal.pop_adjustment(1, 2010)
                              .at(hgps::core::Gender::male, hgps::core::Identifier{"bmi"})
                              .front());

    // A third ask means the intervention's models differ from the baseline's.
    EXPECT_THROW(journal.pop_adjustment(1, 2010), hgps::diag::InternalError);

    journal.reset_adjustment_cursor(1, 2010);
    EXPECT_DOUBLE_EQ(1.0, journal.pop_adjustment(1, 2010)
                              .at(hgps::core::Gender::male, hgps::core::Identifier{"bmi"})
                              .front());
}

TEST(ScenarioJournalTest, RecordsAndReplaysResidualMortality) {
    hgps::sim::ScenarioJournal journal;

    auto table = hgps::model::create_integer_gender_table<double>(hgps::core::IntegerInterval{0, 5});
    table.at(3, hgps::core::Gender::female) = 0.25;

    journal.record_residual_mortality(1, 2010, table);
    EXPECT_TRUE(journal.contains_residual_mortality(1, 2010));
    EXPECT_DOUBLE_EQ(0.25, journal.replay_residual_mortality(1, 2010)
                               .at(3, hgps::core::Gender::female));

    EXPECT_THROW(journal.record_residual_mortality(1, 2010, table), hgps::diag::InternalError);
    EXPECT_THROW(journal.replay_residual_mortality(1, 2099), hgps::diag::InternalError);

    journal.clear();
    EXPECT_FALSE(journal.contains_residual_mortality(1, 2010));
}

TEST_P(TestSimulation, MultipleTrialRunsAppearAndDoNotShareSeeds) {
    auto config_doc = document();
    config_doc["running"]["trial_runs"] = 3;
    config_doc["running"]["stop_time"] = config_doc["running"]["start_time"].get<int>() + 1;
    const auto config = hgps::test::write_config_variant(pack(), "sim_runs_config", config_doc);

    const auto outcome = hgps::test::run_simulation(config, pack_scratch("sim_runs"));
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto rows = read_rows(outcome.csv_path);

    std::set<int> runs;
    for (const auto &row : rows) {
        runs.insert(row.run);
    }
    EXPECT_EQ(std::set<int>({1, 2, 3}), runs);

    // Different runs get different seeds, so their results differ — but not in the factor
    // means. `adjust_to_factors_mean` shifts every value in an (age, sex) band by
    // `expected - simulated_mean`, so each band's mean lands exactly on the FactorsMean table
    // whatever the seed was; the baseline's output has the same property. What the seed moves is
    // the spread around those means, and everything downstream of it.
    for (const auto *channel : {"std_bmi", "std_energy"}) {
        std::set<double> per_run;
        for (int run = 1; run <= 3; ++run) {
            double sum = 0.0;
            for (const auto &row : rows) {
                if (row.run == run) {
                    sum += row.values.at(channel);
                }
            }
            per_run.insert(sum);
        }
        EXPECT_EQ(3U, per_run.size())
            << "three trial runs produced the same " << channel << ", so they shared a seed";
    }

    // And the means really are the same across runs, which is the invariant the calibration
    // promises rather than an accident of this fixture.
    std::set<double> mean_bmi_per_run;
    for (int run = 1; run <= 3; ++run) {
        double sum = 0.0;
        for (const auto &row : rows) {
            if (row.run == run) {
                sum += row.values.at("mean_bmi");
            }
        }
        mean_bmi_per_run.insert(sum);
    }
    EXPECT_EQ(1U, mean_bmi_per_run.size())
        << "the calibrated band means should not depend on the seed";
}

TEST_P(TestSimulation, ADryRunValidatesWithoutWriting) {
    // What --dry-run does, without the CLI: load everything, report, and produce no files.
    hgps::diag::IssueReport report;
    const auto config =
        hgps::config::load(pack().config(), hgps::config::LoadOptions{}, report);

    ASSERT_TRUE(config.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors());

    // The only diagnostic either synthetic config produces is the documented gender2 default.
    EXPECT_EQ(1U, report.warning_count()) << report.to_string();
    EXPECT_TRUE(report.contains(hgps::diag::IssueCode::config_default_applied));
}
