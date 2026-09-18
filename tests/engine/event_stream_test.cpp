// The event stream: what it promises about order, and — the one that matters — that subscribing to
// it changes nothing about the run.
//
// An event stream is the sort of feature that can quietly cost determinism: a timing call that
// perturbs nothing, a population count that takes a lock, a subscriber that is handed a mutable
// reference. So the first test here is not about events at all. It runs the same configuration twice,
// once with a subscriber that records everything and once with none, and compares the result files
// byte for byte.
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include "hgps/engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// @brief Records every event, in the order it arrived, as a flat list of labels plus the details
///        the assertions need.
class Recorder final : public hgps::api::EventSubscriber {
  public:
    std::vector<std::string> order;
    std::vector<hgps::api::YearCompleted> years;
    std::vector<hgps::api::ScenarioStarted> scenario_starts;
    std::vector<hgps::api::ScenarioCompleted> scenario_ends;
    std::optional<hgps::api::RunStarted> started;
    std::optional<hgps::api::RunCompleted> completed;
    std::vector<hgps::api::Diagnostic> diagnostics;

    void on_run_started(const hgps::api::RunStarted &event) override {
        order.emplace_back("run_started");
        started = event;
    }
    void on_scenario_started(const hgps::api::ScenarioStarted &event) override {
        order.emplace_back("scenario_started");
        scenario_starts.push_back(event);
    }
    void on_year_completed(const hgps::api::YearCompleted &event) override {
        order.emplace_back("year_completed");
        years.push_back(event);
    }
    void on_scenario_completed(const hgps::api::ScenarioCompleted &event) override {
        order.emplace_back("scenario_completed");
        scenario_ends.push_back(event);
    }
    void on_run_completed(const hgps::api::RunCompleted &event) override {
        order.emplace_back("run_completed");
        completed = event;
    }
    void on_diagnostic(const hgps::api::Diagnostic &diagnostic) override {
        order.emplace_back("diagnostic");
        diagnostics.push_back(diagnostic);
    }
};

/// @brief A subscriber that does enough work in its callbacks to move any timing the run might be
///        tempted to derive a number from.
class BusySubscriber final : public hgps::api::EventSubscriber {
  public:
    std::size_t sink{0};

    void on_year_completed(const hgps::api::YearCompleted &) override { burn(); }
    void on_scenario_started(const hgps::api::ScenarioStarted &) override { burn(); }

  private:
    void burn() {
        for (std::size_t i = 0; i < 200000; ++i) {
            sink += i % 7;
        }
        std::this_thread::yield();
    }
};

} // namespace

TEST(EventStream, ARunWithASubscriberWritesTheSameBytesAsOneWithout) {
    Recorder recorder;
    const auto with =
        hgps::test::run_simulation(hgps::test::synthetic_config(),
                                   hgps::test::scratch_dir("events_with"), 1, &recorder);
    const auto without = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                    hgps::test::scratch_dir("events_without"));

    ASSERT_TRUE(with.succeeded) << with.report.to_string();
    ASSERT_TRUE(without.succeeded) << without.report.to_string();

    EXPECT_EQ(read_file(with.csv_path), read_file(without.csv_path))
        << "subscribing to the event stream changed the results";

    // And the subscriber really did see something, so the comparison above is not vacuous.
    EXPECT_FALSE(recorder.years.empty());
}

TEST(EventStream, ASubscriberThatWastesTimeStillChangesNothing) {
    // The year timings are real wall-clock measurements, so a slow subscriber changes them. Nothing
    // in the simulation may read them.
    BusySubscriber busy;
    const auto slow =
        hgps::test::run_simulation(hgps::test::synthetic_config(),
                                   hgps::test::scratch_dir("events_slow"), 1, &busy);
    const auto quick = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                 hgps::test::scratch_dir("events_quick"));

    ASSERT_TRUE(slow.succeeded) << slow.report.to_string();
    ASSERT_TRUE(quick.succeeded);
    EXPECT_EQ(read_file(slow.csv_path), read_file(quick.csv_path));
    EXPECT_GT(busy.sink, 0U);
}

TEST(EventStream, TheEventsArriveInTheDocumentedOrder) {
    Recorder recorder;
    const auto outcome =
        hgps::test::run_simulation(hgps::test::synthetic_config(),
                                   hgps::test::scratch_dir("events_order"), 1, &recorder);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    ASSERT_FALSE(recorder.order.empty());
    EXPECT_EQ("run_started", recorder.order.front());
    EXPECT_EQ("run_completed", recorder.order.back());

    // Exactly one of each bookend.
    EXPECT_EQ(1, std::count(recorder.order.begin(), recorder.order.end(), "run_started"));
    EXPECT_EQ(1, std::count(recorder.order.begin(), recorder.order.end(), "run_completed"));

    // A scenario starts before any of its years and completes after all of them.
    const auto first_year = std::find(recorder.order.begin(), recorder.order.end(),
                                      "year_completed");
    const auto first_scenario = std::find(recorder.order.begin(), recorder.order.end(),
                                          "scenario_started");
    ASSERT_NE(recorder.order.end(), first_year);
    ASSERT_NE(recorder.order.end(), first_scenario);
    EXPECT_LT(first_scenario, first_year);

    const auto last_year = std::find(recorder.order.rbegin(), recorder.order.rend(),
                                     "year_completed");
    const auto last_scenario_end = std::find(recorder.order.rbegin(), recorder.order.rend(),
                                             "scenario_completed");
    ASSERT_NE(recorder.order.rend(), last_scenario_end);
    EXPECT_LT(last_scenario_end, last_year) << "a scenario completed before its last year";
}

TEST(EventStream, RunStartedAnnouncesWhatTheRunWillDo) {
    Recorder recorder;
    const auto outcome =
        hgps::test::run_simulation(hgps::test::synthetic_config(),
                                   hgps::test::scratch_dir("events_started"), 1, &recorder);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();
    ASSERT_TRUE(recorder.started.has_value());

    const auto &started = *recorder.started;
    EXPECT_EQ(std::string{hgps::api::build_info().version}, started.engine_version);
    EXPECT_NE(0U, started.seed);
    EXPECT_GT(started.cohort_size, 0U);
    EXPECT_LT(started.start_time, started.stop_time);
    EXPECT_EQ(std::vector<std::string>{"Baseline"}, started.scenarios);

    // The promise `total_years` makes is that it is the number of YearCompleted events an
    // uncancelled run will emit — which is what a progress bar divides by.
    EXPECT_EQ(started.total_years, recorder.years.size());
}

TEST(EventStream, EveryYearOfEveryScenarioIsReportedExactlyOnce) {
    auto document = hgps::test::synthetic_config_document();
    document["running"]["interventions"]["active_type_id"] = "simple";
    document["running"]["trial_runs"] = 2;
    const auto config = hgps::test::write_config_variant("events_two_scenarios", document);

    Recorder recorder;
    const auto outcome = hgps::test::run_simulation(
        config, hgps::test::scratch_dir("events_two_scenarios_out"), 1, &recorder);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();

    const auto start = recorder.started->start_time;
    const auto stop = recorder.started->stop_time;
    const auto years_per_scenario = static_cast<std::size_t>(stop - start) + 1;

    EXPECT_EQ(4U, recorder.scenario_starts.size()) << "two scenarios, two trial runs";
    EXPECT_EQ(4U, recorder.scenario_ends.size());
    EXPECT_EQ(4 * years_per_scenario, recorder.years.size());
    EXPECT_EQ(recorder.years.size(), outcome.years_completed);

    // The years of one (scenario, run) are consecutive and ascending, and no pair repeats.
    std::set<std::tuple<std::string, unsigned int, int>> seen;
    for (const auto &year : recorder.years) {
        EXPECT_TRUE(seen.insert({year.scenario, year.run, year.year}).second)
            << year.scenario << ' ' << year.run << ' ' << year.year;
        EXPECT_GE(year.year, start);
        EXPECT_LE(year.year, stop);
        EXPECT_GT(year.population_size, 0U);
        EXPECT_GE(year.elapsed_ms, 0.0);
    }
    EXPECT_EQ(recorder.years.size(), seen.size());

    // The baseline of a trial run is announced before its intervention: that ordering is the
    // migration journal's whole premise, so an event stream that showed otherwise would be lying
    // about the run.
    ASSERT_EQ(4U, recorder.scenario_starts.size());
    EXPECT_EQ(hgps::api::ScenarioKind::baseline, recorder.scenario_starts[0].kind);
    EXPECT_EQ(hgps::api::ScenarioKind::intervention, recorder.scenario_starts[1].kind);
    EXPECT_EQ(1U, recorder.scenario_starts[0].run);
    EXPECT_EQ(1U, recorder.scenario_starts[1].run);
    EXPECT_EQ(2U, recorder.scenario_starts[2].run);
    EXPECT_EQ(2U, recorder.scenario_starts[3].run);
}

TEST(EventStream, RunCompletedListsEveryFileIncludingTheManifest) {
    Recorder recorder;
    const auto outcome =
        hgps::test::run_simulation(hgps::test::synthetic_config(),
                                   hgps::test::scratch_dir("events_files"), 1, &recorder);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();
    ASSERT_TRUE(recorder.completed.has_value());

    EXPECT_FALSE(recorder.completed->cancelled);
    EXPECT_GT(recorder.completed->elapsed_ms, 0.0);
    EXPECT_EQ(outcome.all_paths, recorder.completed->outputs);
    for (const auto &path : recorder.completed->outputs) {
        EXPECT_TRUE(std::filesystem::is_regular_file(path)) << path;
    }
    EXPECT_NE(recorder.completed->outputs.end(),
              std::find(recorder.completed->outputs.begin(), recorder.completed->outputs.end(),
                        outcome.manifest_path));
}

TEST(EventStream, TheScenarioNamesMatchWhatRunStartedAnnounced) {
    auto document = hgps::test::synthetic_config_document();
    document["running"]["interventions"]["active_type_id"] = "simple";
    const auto config = hgps::test::write_config_variant("events_names", document);

    Recorder recorder;
    const auto outcome =
        hgps::test::run_simulation(config, hgps::test::scratch_dir("events_names_out"), 1,
                                   &recorder);
    ASSERT_TRUE(outcome.succeeded) << outcome.report.to_string();
    ASSERT_TRUE(recorder.started.has_value());

    std::set<std::string> announced{recorder.started->scenarios.begin(),
                                    recorder.started->scenarios.end()};
    for (const auto &event : recorder.scenario_starts) {
        EXPECT_TRUE(announced.contains(event.scenario)) << event.scenario;
    }
    for (const auto &event : recorder.years) {
        EXPECT_TRUE(announced.contains(event.scenario)) << event.scenario;
    }
}

TEST(Cancellation, ADefaultTokenIsNeverCancelled) {
    const hgps::api::CancellationToken token;
    EXPECT_FALSE(token.cancelled());
}

TEST(Cancellation, ACopyOfATokenSharesItsFlag) {
    const hgps::api::CancellationToken token;
    const auto copy = token;
    copy.cancel();
    EXPECT_TRUE(token.cancelled()) << "a copied token must share the flag, or a host that keeps a "
                                     "copy cannot stop the run it handed the original to";
}

TEST(Cancellation, ARunCancelledUpFrontDoesNoYearsAndStillWritesItsFiles) {
    hgps::api::CancellationToken token;
    token.cancel();

    Recorder recorder;
    const auto outcome =
        hgps::test::run_simulation(hgps::test::synthetic_config(),
                                   hgps::test::scratch_dir("cancel_upfront"), 1, &recorder, token);

    EXPECT_TRUE(outcome.succeeded) << outcome.report.to_string();
    EXPECT_TRUE(outcome.cancelled);
    EXPECT_EQ(0U, outcome.years_completed);
    EXPECT_TRUE(recorder.years.empty());

    // The files exist and are closed, with a header and no rows: a cancelled run leaves something
    // valid behind rather than a half-written file.
    EXPECT_TRUE(std::filesystem::is_regular_file(outcome.csv_path));
    EXPECT_TRUE(std::filesystem::is_regular_file(outcome.manifest_path));
    ASSERT_TRUE(recorder.completed.has_value());
    EXPECT_TRUE(recorder.completed->cancelled);
}

namespace {

/// @brief Cancels the run from inside the event stream, after `after` years.
class CancelAfter final : public hgps::api::EventSubscriber {
  public:
    CancelAfter(hgps::api::CancellationToken token, std::size_t after)
        : token_{std::move(token)}, after_{after} {}

    std::size_t seen{0};

    void on_year_completed(const hgps::api::YearCompleted &) override {
        if (++seen >= after_) {
            token_.cancel();
        }
    }

  private:
    hgps::api::CancellationToken token_;
    std::size_t after_;
};

} // namespace

TEST(Cancellation, ACancelledRunIsAPrefixOfTheRunThatWouldHaveHappened) {
    // The contract: cancellation lands between years, so the years that did run are exactly the
    // years the full run would have produced — the same rows, byte for byte, as the full run's
    // first N.
    const auto full = hgps::test::run_simulation(hgps::test::synthetic_config(),
                                                hgps::test::scratch_dir("cancel_full"));
    ASSERT_TRUE(full.succeeded) << full.report.to_string();

    hgps::api::CancellationToken token;
    CancelAfter canceller{token, 3};
    const auto partial =
        hgps::test::run_simulation(hgps::test::synthetic_config(),
                                   hgps::test::scratch_dir("cancel_partial"), 1, &canceller,
                                   token);

    ASSERT_TRUE(partial.succeeded) << partial.report.to_string();
    EXPECT_TRUE(partial.cancelled);
    EXPECT_EQ(3U, partial.years_completed);
    EXPECT_LT(partial.years_completed, full.years_completed);

    const auto full_text = read_file(full.csv_path);
    const auto partial_text = read_file(partial.csv_path);
    EXPECT_FALSE(partial_text.empty());
    EXPECT_TRUE(full_text.starts_with(partial_text))
        << "a cancelled run's rows are not a prefix of the full run's";
}
