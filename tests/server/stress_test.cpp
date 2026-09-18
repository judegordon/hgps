// The server's lifetime, under load, in a randomised order — the shape of test that finds the
// faults reading found and endpoint tests did not.
//
// Three of the previous run's defects were lifetime or concurrency faults in this layer, and all
// three were found by reading the code back rather than by running it: two clients validating the
// same document picked the same scratch filename, a server that was started and then dropped called
// `std::terminate`, and stopping the server left a run thread writing while `main` returned. Each
// has a test now. Every one of those tests is a reproduction of a known fault, which is the
// weakest kind of test there is: it can only fail for the reason it was written for.
//
// This one is the other kind. It does not know what it is looking for. N clients validate at once
// while runs start, get cancelled and get stopped in an order decided by a seeded shuffle, and what
// it asserts is that nothing crashes, every answer is one the contract allows, and the server is
// coherent afterwards. Under ThreadSanitizer — which `scripts/check.sh` and the `tsan` CI job both
// run — a data race anywhere in that is a failure with a stack on both sides of it.
//
// **It has to stay quick.** The macOS ThreadSanitizer job takes about three quarters of an hour on
// a shared runner already (docs/build-notes.md). Everything here is sized so that the whole file is
// well under two minutes even there: the synthetic pack's runs are short, the client counts are
// small, and nothing sleeps for a fixed period where it could wait for a condition.
#include "support.h"

#include "hgps/engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::test::ServedFixture;

/// @brief The seed the orders are shuffled with.
///
/// Fixed, and printed on failure, because a stress test that cannot be re-run is a bug report
/// nobody can act on. The variety comes from doing several different shuffles in one run rather
/// than from a different seed each time.
constexpr std::uint32_t kSeed = 0x5eed'1234U;

/// @brief What the run-lifecycle thread does, in whatever order the shuffle gives it.
enum class Action { start, cancel, list, inspect, stop_wait };

const char *name_of(Action action) {
    switch (action) {
    case Action::start:
        return "start";
    case Action::cancel:
        return "cancel";
    case Action::list:
        return "list";
    case Action::inspect:
        return "inspect";
    case Action::stop_wait:
        return "stop_wait";
    }
    return "?";
}

/// Both packs, so the sequences cover a one-scenario run and a two-scenario one — the second takes
/// longer, which changes which of these actions land while a run is going.
class ServerStress : public hgps::test::ServedPackTest {};

HGPS_TEST_EVERY_FIXTURE_PACK(ServerStress);

} // namespace

TEST_P(ServerStress, ManyClientsValidateWhileRunsStartAndStopInARandomisedOrder) {
    ServedFixture served{"stress_" + pack().id};

    const auto document =
        json_body_of(served.client().Get(std::string{"/api/examples/"} + example()))
            .at("document");
    const nlohmann::json validate_body{{"document", document}, {"base", example()}};
    const auto payload = validate_body.dump();

    // Six validating clients and one lifecycle thread. Six because the scratch-file collision the
    // previous run found needs only two, and more threads under ThreadSanitizer buy contention
    // rather than coverage.
    constexpr int kValidators = 6;
    // A cap rather than a count: the validators keep going until the lifecycle thread is done, so
    // the contention lasts as long as the thing it is contending with. The cap is there only so a
    // lifecycle that somehow never finishes does not spin for ever.
    constexpr int kValidationCap = 500;
    // Three permutations per pack, six across the two. Sized by the clock rather than by taste:
    // each sequence can run a whole simulation, which under ThreadSanitizer is seconds rather than
    // a fifth of one, and this file has to stay well inside the two minutes the macOS TSan job can
    // afford. Six sequences measured 78 seconds there; three measure about half of that.
    constexpr int kSequences = 3;

    std::atomic<bool> lifecycle_done{false};
    std::atomic<int> validated{0};
    std::atomic<int> unexpected_validation{0};
    std::string first_unexpected;
    std::mutex report_mutex;

    const auto note = [&](const std::string &what) {
        const std::lock_guard lock{report_mutex};
        if (first_unexpected.empty()) {
            first_unexpected = what;
        }
    };

    std::vector<std::thread> validators;
    validators.reserve(kValidators);
    for (int client = 0; client < kValidators; ++client) {
        validators.emplace_back([&] {
            auto http = served.client();
            for (int i = 0; i < kValidationCap && !lifecycle_done.load(); ++i) {
                const auto response =
                    http.Post("/api/configs/validate", payload, "application/json");
                if (!response) {
                    unexpected_validation.fetch_add(1);
                    note("no response to validate");
                    continue;
                }
                if (response->status != 200) {
                    unexpected_validation.fetch_add(1);
                    note("validate answered " + std::to_string(response->status) + ": " +
                         response->body);
                    continue;
                }
                if (!ServedFixture::json_of(response).value("valid", false)) {
                    unexpected_validation.fetch_add(1);
                    note("a good document was reported invalid: " + response->body);
                    continue;
                }
                validated.fetch_add(1);
            }
        });
    }

    // The lifecycle, in a shuffled order each time round. Every response is checked against what
    // the contract allows for that request *whatever* the server's state is, which is the only
    // assertion available when the order is not known in advance.
    std::mt19937 shuffle{kSeed};
    std::vector<Action> actions{Action::start,   Action::cancel,   Action::list,
                                Action::inspect, Action::stop_wait};
    std::set<std::string> started;
    std::string current;
    std::string order_taken;

    auto http = served.client();
    for (int sequence = 0; sequence < kSequences; ++sequence) {
        std::shuffle(actions.begin(), actions.end(), shuffle);
        for (const auto action : actions) {
            order_taken += std::string{name_of(action)} + " ";
            switch (action) {
            case Action::start: {
                const nlohmann::json body{{"example", example()}};
                const auto response = http.Post("/api/runs", body.dump(), "application/json");
                ASSERT_TRUE(response) << "no response to a start; order so far: " << order_taken;
                // 201 when the slot was free, 409 when it was not. Nothing else.
                ASSERT_TRUE(response->status == 201 || response->status == 409)
                    << "start answered " << response->status << ": " << response->body
                    << "\norder so far: " << order_taken;
                if (response->status == 201) {
                    current = ServedFixture::json_of(response).at("id").get<std::string>();
                    started.insert(current);
                } else {
                    EXPECT_EQ("run_in_progress",
                              ServedFixture::json_of(response).at("error").at("code"));
                }
                break;
            }
            case Action::cancel: {
                if (current.empty()) {
                    break;
                }
                const auto response =
                    http.Post("/api/runs/" + current + "/cancel", "{}", "application/json");
                ASSERT_TRUE(response) << "no response to a cancel; order so far: " << order_taken;
                // 202 while it is going, 409 once it has finished. A 404 would mean the run was
                // forgotten, which is the fault this is watching for.
                ASSERT_TRUE(response->status == 202 || response->status == 409)
                    << "cancel answered " << response->status << ": " << response->body
                    << "\norder so far: " << order_taken;
                break;
            }
            case Action::list: {
                const auto response = http.Get("/api/runs");
                ASSERT_TRUE(response) << "no response to a list; order so far: " << order_taken;
                ASSERT_EQ(200, response->status) << response->body;
                const auto listing = ServedFixture::json_of(response);
                ASSERT_TRUE(listing.contains("runs")) << response->body;
                // Every run that was ever accepted is either the active one or in the history.
                // A run that vanishes is the `starting` for ever fault from the other direction.
                std::set<std::string> visible;
                for (const auto &run : listing.at("runs")) {
                    visible.insert(run.value("id", std::string{}));
                }
                for (const auto &id : started) {
                    EXPECT_TRUE(visible.contains(id))
                        << id << " was accepted and is in neither the active slot nor the history;"
                        << " order so far: " << order_taken;
                }
                break;
            }
            case Action::inspect: {
                if (current.empty()) {
                    break;
                }
                const auto response = http.Get("/api/runs/" + current);
                ASSERT_TRUE(response) << "no response to an inspect; order: " << order_taken;
                ASSERT_EQ(200, response->status) << response->body;
                const auto state =
                    ServedFixture::json_of(response).value("state", std::string{});
                EXPECT_TRUE(state == "starting" || state == "running" || state == "completed" ||
                            state == "cancelled" || state == "failed")
                    << "unknown state '" << state << "'; order so far: " << order_taken;
                break;
            }
            case Action::stop_wait: {
                if (current.empty()) {
                    break;
                }
                // Waiting for the run to reach a terminal state, rather than sleeping: the point of
                // the next sequence is that it starts from a known place.
                const auto finished = served.wait_for_run(current, std::chrono::seconds{90});
                const auto state = finished.value("state", std::string{});
                ASSERT_TRUE(state == "completed" || state == "cancelled" || state == "failed")
                    << "a run did not reach a terminal state: " << finished.dump()
                    << "\norder so far: " << order_taken;
                current.clear();
                break;
            }
            }
        }
    }

    // Whatever the order did, the last run must be allowed to finish before the fixture takes the
    // server down, or this test becomes a test of the destructor instead.
    if (!current.empty()) {
        served.wait_for_run(current, std::chrono::seconds{90});
    }

    lifecycle_done.store(true);
    for (auto &thread : validators) {
        thread.join();
    }

    EXPECT_EQ(0, unexpected_validation.load())
        << first_unexpected << "\nseed " << kSeed << ", order: " << order_taken;
    EXPECT_GT(validated.load(), 0) << "no validation completed, so this proved nothing";

    // And the server is still answering, with a history that parses and no run left active.
    const auto listing = json_body_of(http.Get("/api/runs"));
    EXPECT_TRUE(listing.at("active").is_null())
        << "a run was left active: " << listing.dump(2) << "\norder: " << order_taken;
    EXPECT_FALSE(started.empty());

    // Every scratch file the validations made was cleaned up, whichever client won each race.
    int leftovers = 0;
    for (const auto &entry :
         std::filesystem::directory_iterator{served.configs() / example()}) {
        if (entry.path().filename().string().starts_with(".hgps-validate-")) {
            ++leftovers;
        }
    }
    EXPECT_EQ(0, leftovers) << "scratch configs were left behind; order: " << order_taken;
}

TEST(ServerLifetime, AServerStoppedBeforeItHasServedAnythingStillStops) {
    // The race the shuffled sequence above found, pinned so that it does not depend on a shuffle.
    // cpp-httplib's `stop()` does nothing unless the server is already running, and `is_running_`
    // is set by the serving thread — so a `start()` that returned as soon as the thread was spawned
    // could be followed by a `stop()` that was lost, and the join then blocked for ever.
    //
    // Nothing happens between the start and the stop here, deliberately: a request in between is
    // what hid this, because an answer proves the accept loop is running.
    for (int attempt = 0; attempt < 8; ++attempt) {
        hgps::server::Options options;
        options.host = "127.0.0.1";
        options.port = 0;
        options.runs_root =
            hgps::test::scratch_dir("stress_immediate_" + std::to_string(attempt));

        hgps::server::Server server{options};
        ASSERT_NE(0, server.start()) << "attempt " << attempt;
        server.stop();
    }
    // Reaching here rather than hanging is the assertion.
    SUCCEED();
}

TEST(ServerLifetime, AServerDroppedBeforeItHasServedAnythingStillUnwinds) {
    // The same, through the destructor, which is the path a host that forgets `stop()` takes.
    for (int attempt = 0; attempt < 8; ++attempt) {
        hgps::server::Options options;
        options.host = "127.0.0.1";
        options.port = 0;
        options.runs_root = hgps::test::scratch_dir("stress_dropped_" + std::to_string(attempt));

        hgps::server::Server server{options};
        ASSERT_NE(0, server.start()) << "attempt " << attempt;
    }
    SUCCEED();
}

TEST_P(ServerStress, AServerCanBeStoppedOrDroppedAtAnyPointOfARun) {
    // The other half: the server object's own lifetime, against a run that is going. `stop()`
    // cancels and joins; the destructor has to do the same for a server that is simply dropped, and
    // the previous run found that it did not. Here the moment of the stop is chosen by the shuffle
    // rather than by the test, so it lands before, during and after the run across the sequences.
    std::mt19937 shuffle{kSeed};
    std::uniform_int_distribution<int> when{0, 3};

    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto runs = hgps::test::scratch_dir("stress_stop_runs_" + pack().id + "_" +
                                                  std::to_string(attempt));
        const auto configs = hgps::test::scratch_dir("stress_stop_configs_" + pack().id + "_" +
                                                     std::to_string(attempt));
        const auto recursive = std::filesystem::copy_options::recursive |
                               std::filesystem::copy_options::overwrite_existing;
        std::filesystem::copy(pack().directory, configs / example(), recursive);
        std::filesystem::copy(hgps::test::synthetic_data_dir(), configs / "data", recursive);

        hgps::server::Options options;
        options.host = "127.0.0.1";
        options.port = 0;
        options.runs_root = runs;
        options.config_roots = {configs};

        const auto moment = when(shuffle);
        const bool drop_rather_than_stop = (attempt % 2) == 1;
        std::string id;
        {
            hgps::server::Server server{options};
            const auto port = server.start();
            ASSERT_NE(0, port) << "attempt " << attempt;

            httplib::Client client{"127.0.0.1", port};
            client.set_read_timeout(std::chrono::seconds{90});

            if (moment > 0) {
                const nlohmann::json body{{"example", example()}};
                const auto created = client.Post("/api/runs", body.dump(), "application/json");
                ASSERT_TRUE(created) << "attempt " << attempt;
                ASSERT_EQ(201, created->status) << created->body;
                id = json_body_of(created).at("id").get<std::string>();
            }
            if (moment > 1 && !id.empty()) {
                // Asked to stop, then stopped: the two paths must not fight over the run's thread.
                client.Post("/api/runs/" + id + "/cancel", "{}", "application/json");
            }
            if (moment > 2 && !id.empty()) {
                const auto response = client.Get("/api/runs/" + id);
                ASSERT_TRUE(response) << "attempt " << attempt;
            }

            if (!drop_rather_than_stop) {
                server.stop();
            }
            // …and otherwise it goes out of scope here, joinable thread and running simulation and
            // all. Reaching the next line is most of the assertion.
        }

        if (id.empty()) {
            continue;
        }

        // What is on disk is a prefix of the run that would have happened, with its files closed:
        // a manifest, if there is one, parses.
        for (const auto &entry : std::filesystem::directory_iterator{runs / id}) {
            if (!entry.path().filename().string().ends_with("_manifest.json")) {
                continue;
            }
            std::ifstream stream{entry.path()};
            nlohmann::json manifest;
            ASSERT_NO_THROW(stream >> manifest)
                << "attempt " << attempt << " left a half-written manifest";
            EXPECT_TRUE(manifest.contains("run"));
        }
    }
}
