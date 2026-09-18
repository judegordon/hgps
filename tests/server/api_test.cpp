// Every endpoint docs/server-api.md publishes, driven over a real socket.
//
// In process, on a free port, against the synthetic fixture pack — so the whole API is exercised
// by `ctest` rather than by somebody starting the binary and reaching for curl. An HTTP API whose
// only test is "the binary starts" is an HTTP API with no tests.
#include "support.h"

#include "hgps/engine.h"

#include <algorithm>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace {

using hgps::test::ServedFixture;

nlohmann::json json_body(const httplib::Result &result) {
    return ServedFixture::json_of(result);
}

} // namespace

// --- version ------------------------------------------------------------------------------------

TEST(ServerApi, VersionReportsTheEngineAndTheCompatibilityFlags) {
    ServedFixture served{"api_version"};
    auto client = served.client();

    const auto response = client.Get("/api/version");
    ASSERT_TRUE(response) << "no response";
    EXPECT_EQ(200, response->status);
    EXPECT_EQ("no-store", response->get_header_value("Cache-Control"));

    const auto document = json_body(response);
    EXPECT_EQ(1, document.at("api_version").get<int>());
    EXPECT_FALSE(document.at("engine_version").get<std::string>().empty());

    // The flags are listed here so a client need not hard-code them (ADR 0041).
    const auto flags = document.at("baseline_compat_flags");
    ASSERT_EQ(hgps::api::BaselineCompat::flag_count, flags.size());
    EXPECT_EQ("B-24", flags.at(0).at("name").get<std::string>());
    EXPECT_FALSE(flags.at(0).at("description").get<std::string>().empty());
}

// --- examples -----------------------------------------------------------------------------------

TEST(ServerApi, ExamplesListsWhatTheConfigRootsHold) {
    ServedFixture served{"api_examples"};
    auto client = served.client();

    const auto document = json_body(client.Get("/api/examples"));
    const auto examples = document.at("examples");
    ASSERT_EQ(1U, examples.size());
    EXPECT_EQ(ServedFixture::kExample, examples.at(0).at("id").get<std::string>());
    EXPECT_TRUE(examples.at(0).at("readable").get<bool>());
}

TEST(ServerApi, OneExampleIsLoadedAndSummarised) {
    ServedFixture served{"api_example_one"};
    auto client = served.client();

    const auto response = client.Get(std::string{"/api/examples/"} + ServedFixture::kExample);
    ASSERT_TRUE(response);
    ASSERT_EQ(200, response->status) << response->body;

    const auto document = json_body(response);
    EXPECT_EQ(ServedFixture::kExample, document.at("id").get<std::string>());

    // The document as written, for an editor to open…
    EXPECT_TRUE(document.at("document").contains("running"));
    // …and what the engine understood, which is not the same thing.
    const auto summary = document.at("summary");
    EXPECT_EQ(123456789U, summary.at("seed").get<std::uint32_t>());
    EXPECT_EQ(2010, summary.at("start_time").get<int>());
    EXPECT_TRUE(summary.at("active_intervention").is_null());
    EXPECT_TRUE(summary.at("baseline_compat").empty());
}

TEST(ServerApi, AnExampleThatIsNotThereIsA404AndNotAPathToTraverse) {
    ServedFixture served{"api_example_missing"};
    auto client = served.client();

    for (const auto *id : {"nope", "..", "%2e%2e"}) {
        const auto response = client.Get(std::string{"/api/examples/"} + id);
        ASSERT_TRUE(response) << id;
        EXPECT_EQ(404, response->status) << id << ": " << response->body;
        EXPECT_EQ("not_found", json_body(response).at("error").at("code").get<std::string>()) << id;
    }
}

// --- validation ---------------------------------------------------------------------------------

TEST(ServerApi, ValidateAcceptsAGoodDocument) {
    ServedFixture served{"api_validate_ok"};
    auto client = served.client();

    const auto example =
        json_body(client.Get(std::string{"/api/examples/"} + ServedFixture::kExample));
    const nlohmann::json body{{"document", example.at("document")},
                              {"base", ServedFixture::kExample}};

    const auto response = client.Post("/api/configs/validate", body.dump(), "application/json");
    ASSERT_TRUE(response);
    ASSERT_EQ(200, response->status) << response->body;

    const auto document = json_body(response);
    EXPECT_TRUE(document.at("valid").get<bool>()) << document.dump(2);
    EXPECT_EQ(0U, document.at("error_count").get<std::size_t>());
    EXPECT_FALSE(document.at("summary").is_null());
}

TEST(ServerApi, ValidateLocatesEveryProblemAtTheFieldThatCausedIt) {
    ServedFixture served{"api_validate_bad"};
    auto client = served.client();

    auto document =
        json_body(client.Get(std::string{"/api/examples/"} + ServedFixture::kExample))
            .at("document");
    document["running"].erase("seed");
    document["running"]["stop_time"] = "not a year";

    const nlohmann::json body{{"document", document}, {"base", ServedFixture::kExample}};
    const auto response = client.Post("/api/configs/validate", body.dump(), "application/json");
    ASSERT_TRUE(response);
    // Not an error of the request: an invalid document is the *answer*, and a 200 says so.
    ASSERT_EQ(200, response->status) << response->body;

    const auto result = json_body(response);
    EXPECT_FALSE(result.at("valid").get<bool>());
    EXPECT_GE(result.at("error_count").get<std::size_t>(), 2U);

    // The pointer is the whole point: an editor puts the message on the field it names.
    bool located = false;
    for (const auto &diagnostic : result.at("diagnostics")) {
        const auto pointer = diagnostic.at("location").value("pointer", std::string{});
        if (pointer.find("stop_time") != std::string::npos ||
            pointer.find("seed") != std::string::npos) {
            located = true;
            EXPECT_FALSE(diagnostic.at("message").get<std::string>().empty());
            EXPECT_FALSE(diagnostic.at("code").get<std::string>().empty());
        }
    }
    EXPECT_TRUE(located) << "no diagnostic named the field it was about: " << result.dump(2);
}

TEST(ServerApi, ValidateCanBeToldNotToInsistTheFilesExistYet) {
    ServedFixture served{"api_validate_files"};
    auto client = served.client();

    auto document =
        json_body(client.Get(std::string{"/api/examples/"} + ServedFixture::kExample))
            .at("document");
    document["inputs"]["dataset"]["name"] = "not-written-yet.csv";

    const auto ask = [&](bool require) {
        const nlohmann::json body{{"document", document},
                                  {"base", ServedFixture::kExample},
                                  {"require_files_exist", require}};
        return json_body(client.Post("/api/configs/validate", body.dump(), "application/json"));
    };

    EXPECT_FALSE(ask(true).at("valid").get<bool>());
    EXPECT_TRUE(ask(false).at("valid").get<bool>()) << "an editor validating a half-written "
                                                       "document should not be told its files "
                                                       "are missing";
}

TEST(ServerApi, ValidateRefusesABodyThatIsNotOne) {
    ServedFixture served{"api_validate_body"};
    auto client = served.client();

    for (const auto *body : {"not json", R"({"no":"document"})"}) {
        const auto response = client.Post("/api/configs/validate", body, "application/json");
        ASSERT_TRUE(response) << body;
        EXPECT_EQ(400, response->status) << body;
        EXPECT_EQ("bad_request", json_body(response).at("error").at("code").get<std::string>());
    }
}

// --- schema -------------------------------------------------------------------------------------

TEST(ServerApi, TheSchemaIsServedWithItsReferencesResolved) {
    ServedFixture served{"api_schema"};
    auto client = served.client();

    const auto response = client.Get("/api/schema");
    ASSERT_TRUE(response);
    ASSERT_EQ(200, response->status) << response->body;

    const auto schema = json_body(response).at("schema");
    const auto properties = schema.at("properties");

    // A form generator that had to fetch transitively would have a loading state per field, so the
    // $refs are gone and the referenced documents are here.
    EXPECT_TRUE(properties.at("running").contains("properties"))
        << "running is still a $ref: " << properties.at("running").dump();
    EXPECT_FALSE(properties.at("running").contains("$ref"));
    EXPECT_TRUE(properties.contains("baseline_compat"));

    std::string text = schema.dump();
    EXPECT_EQ(std::string::npos, text.find("\"$ref\""))
        << "an unresolved $ref would generate a form with silently missing fields";
}

// --- runs ---------------------------------------------------------------------------------------

TEST(ServerApi, ARunGoesFromStartingToCompletedAndWritesItsFiles) {
    ServedFixture served{"api_run"};
    auto client = served.client();

    const nlohmann::json body{{"example", ServedFixture::kExample}};
    const auto created = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_TRUE(created);
    ASSERT_EQ(201, created->status) << created->body;

    const auto start = json_body(created);
    const auto id = start.at("id").get<std::string>();
    EXPECT_FALSE(id.empty());

    // Built before the response was sent, so a 201 means the run will almost certainly finish.
    ASSERT_FALSE(start.at("description").is_null()) << start.dump(2);
    EXPECT_GT(start.at("description").at("cohort_size").get<std::size_t>(), 0U);

    const auto finished = served.wait_for_run(id);
    ASSERT_EQ("completed", finished.value("state", std::string{})) << finished.dump(2);
    EXPECT_FALSE(finished.at("manifest").is_null());

    const auto results = finished.at("results");
    EXPECT_NE(results.end(), std::find(results.begin(), results.end(), "result.csv"));
    EXPECT_NE(results.end(), std::find(results.begin(), results.end(), "result_manifest.json"));
}

TEST(ServerApi, ASecondRunIsRefusedRatherThanQueued) {
    // The engine's own contract: two `execute` calls must not overlap in one process
    // (docs/api.md). A queue would turn a stated constraint into an unstated wait.
    ServedFixture served{"api_run_second"};
    auto client = served.client();

    const nlohmann::json body{{"example", ServedFixture::kExample}};
    const auto first = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_TRUE(first);
    ASSERT_EQ(201, first->status) << first->body;
    const auto id = json_body(first).at("id").get<std::string>();

    const auto second = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_TRUE(second);
    if (second->status == 409) {
        const auto error = json_body(second).at("error");
        EXPECT_EQ("run_in_progress", error.at("code").get<std::string>());
        EXPECT_NE(std::string::npos, error.at("message").get<std::string>().find(id))
            << "the refusal should name the run that is going: " << error.dump();
    } else {
        // The first run finished before the second request arrived, which is possible on the
        // synthetic pack and is not a failure — but then it must have been accepted properly.
        EXPECT_EQ(201, second->status) << second->body;
    }

    served.wait_for_run(id);
}

TEST(ServerApi, ARunTakesTheCompatibilityFlagsAndTheManifestRecordsThem) {
    ServedFixture served{"api_run_compat"};
    auto client = served.client();

    const nlohmann::json body{{"example", ServedFixture::kExample},
                              {"baseline_compat", nlohmann::json::array({"B-24"})}};
    const auto created = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_TRUE(created);
    ASSERT_EQ(201, created->status) << created->body;

    const auto finished = served.wait_for_run(json_body(created).at("id").get<std::string>());
    ASSERT_EQ("completed", finished.value("state", std::string{})) << finished.dump(2);
    EXPECT_EQ(nlohmann::json::array({"B-24"}), finished.at("manifest").at("baseline_compat"));
}

TEST(ServerApi, AnUnknownCompatibilityFlagIsRefusedBeforeAnythingRuns) {
    ServedFixture served{"api_run_bad_compat"};
    auto client = served.client();

    const nlohmann::json body{{"example", ServedFixture::kExample},
                              {"baseline_compat", nlohmann::json::array({"B-99"})}};
    const auto response = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_TRUE(response);
    EXPECT_EQ(400, response->status) << response->body;

    // And the slot was released, so the next run is not refused for a run that never started.
    const nlohmann::json good{{"example", ServedFixture::kExample}};
    const auto next = client.Post("/api/runs", good.dump(), "application/json");
    ASSERT_TRUE(next);
    EXPECT_EQ(201, next->status) << next->body;
    served.wait_for_run(json_body(next).at("id").get<std::string>());
}

TEST(ServerApi, RunsListsTheHistoryFromManifestsAndSurvivesARestart) {
    const std::string name = "api_run_history";
    std::string id;
    std::filesystem::path runs;
    std::filesystem::path configs;
    {
        ServedFixture served{name};
        auto client = served.client();
        const nlohmann::json body{{"example", ServedFixture::kExample}};
        const auto created = client.Post("/api/runs", body.dump(), "application/json");
        ASSERT_TRUE(created);
        ASSERT_EQ(201, created->status) << created->body;
        id = json_body(created).at("id").get<std::string>();
        ASSERT_EQ("completed", served.wait_for_run(id).value("state", std::string{}));

        // Captured rather than recomputed: `scratch_dir` empties what it returns, so asking for
        // the same name again would delete the very history this test is about.
        runs = served.runs();
        configs = served.configs();
    }

    // A second server over the same runs directory. There is no database: a completed run is its
    // manifest, which is what makes the history survive a restart (ADR 0034, ADR 0042).
    hgps::server::Options options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.runs_root = runs;
    options.config_roots = {configs};
    hgps::server::Server restarted{options};
    const auto port = restarted.start();
    ASSERT_NE(0, port);

    httplib::Client client{"127.0.0.1", port};
    const auto listed = json_body(client.Get("/api/runs"));
    restarted.stop();

    EXPECT_TRUE(listed.at("active").is_null());
    bool found = false;
    for (const auto &run : listed.at("runs")) {
        if (run.value("id", std::string{}) == id) {
            found = true;
            EXPECT_EQ("completed", run.value("state", std::string{}));
        }
    }
    EXPECT_TRUE(found) << "the run was not in the restarted server's list: " << listed.dump(2);
}

TEST(ServerApi, CancelIsAcceptedRatherThanAcknowledged) {
    ServedFixture served{"api_cancel"};
    auto client = served.client();

    const nlohmann::json body{{"example", ServedFixture::kExample}};
    const auto created = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_TRUE(created);
    ASSERT_EQ(201, created->status) << created->body;
    const auto id = json_body(created).at("id").get<std::string>();

    const auto cancelled = client.Post("/api/runs/" + id + "/cancel", "", "application/json");
    ASSERT_TRUE(cancelled);
    // 202: the engine observes cancellation at the end of the year it is in, so this response
    // cannot honestly say the run has stopped (docs/api.md).
    if (cancelled->status == 202) {
        EXPECT_FALSE(json_body(cancelled).at("note").get<std::string>().empty());
    } else {
        EXPECT_EQ(409, cancelled->status) << "the run had already finished: " << cancelled->body;
        EXPECT_EQ("run_not_active",
                  json_body(cancelled).at("error").at("code").get<std::string>());
    }

    const auto finished = served.wait_for_run(id);
    const auto state = finished.value("state", std::string{});
    EXPECT_TRUE(state == "cancelled" || state == "completed") << finished.dump(2);

    // Cancelling something that has finished is a 409, not a silent success.
    const auto again = client.Post("/api/runs/" + id + "/cancel", "", "application/json");
    ASSERT_TRUE(again);
    EXPECT_EQ(409, again->status) << again->body;
}

TEST(ServerApi, AResultFileIsServedByNameAndNothingElseIs) {
    ServedFixture served{"api_results"};
    auto client = served.client();

    const nlohmann::json body{{"example", ServedFixture::kExample}};
    const auto created = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_EQ(201, created->status) << created->body;
    const auto id = json_body(created).at("id").get<std::string>();
    ASSERT_EQ("completed", served.wait_for_run(id).value("state", std::string{}));

    const auto csv = client.Get("/api/runs/" + id + "/results/result.csv");
    ASSERT_TRUE(csv);
    ASSERT_EQ(200, csv->status);
    EXPECT_NE(std::string::npos, csv->get_header_value("Content-Type").find("text/csv"));
    EXPECT_NE(std::string::npos,
              csv->get_header_value("Content-Disposition").find("result.csv"));
    EXPECT_TRUE(csv->body.starts_with("source,run,time")) << csv->body.substr(0, 80);

    // A client names a run and a file *name*; there is nothing here to traverse.
    for (const auto *name : {"nope.csv", "..", "config.json"}) {
        const auto refused = client.Get(std::string{"/api/runs/"} + id + "/results/" + name);
        ASSERT_TRUE(refused) << name;
        EXPECT_EQ(404, refused->status) << name << ": " << refused->body;
    }
}

TEST(ServerApi, TheSummaryReducesTheResultCsvForCharting) {
    ServedFixture served{"api_summary"};
    auto client = served.client();

    const nlohmann::json body{{"example", ServedFixture::kExample}};
    const auto created = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_EQ(201, created->status) << created->body;
    const auto id = json_body(created).at("id").get<std::string>();
    ASSERT_EQ("completed", served.wait_for_run(id).value("state", std::string{}));

    const auto response = client.Get("/api/runs/" + id + "/summary");
    ASSERT_TRUE(response);
    ASSERT_EQ(200, response->status) << response->body;

    const auto document = json_body(response);
    const auto years = document.at("years");
    ASSERT_FALSE(years.empty());
    EXPECT_EQ(2010, years.front().get<int>());

    ASSERT_FALSE(document.at("series").empty());
    for (const auto &series : document.at("series")) {
        // Parallel to `years`, so a client plots it without a lookup.
        EXPECT_EQ(years.size(), series.at("values").size())
            << series.at("variable").get<std::string>();
    }

    // Narrowing works, and narrowing to one variable gives one variable.
    const auto narrowed = json_body(client.Get("/api/runs/" + id + "/summary?variable=mean_bmi"));
    EXPECT_EQ(nlohmann::json::array({"mean_bmi"}), narrowed.at("variables"));

    const auto male = json_body(client.Get("/api/runs/" + id + "/summary?sex=male"));
    EXPECT_EQ("male", male.at("sex").get<std::string>());

    const auto refused = client.Get("/api/runs/" + id + "/summary?sex=other");
    ASSERT_TRUE(refused);
    EXPECT_EQ(400, refused->status);
}

TEST(ServerApi, AnUnknownApiPathIsA404AndNotTheIndexPage) {
    // The single most confusing thing a JSON client can be given is an HTML page with a 200.
    ServedFixture served{"api_unknown"};
    auto client = served.client();

    const auto response = client.Get("/api/nope");
    ASSERT_TRUE(response);
    EXPECT_EQ(404, response->status);
    EXPECT_EQ("not_found", json_body(response).at("error").at("code").get<std::string>());
}

// --- the event stream ----------------------------------------------------------------------------

TEST(ServerApi, TheEventStreamReplaysAndThenEnds) {
    ServedFixture served{"api_events"};
    auto client = served.client();

    const nlohmann::json body{{"example", ServedFixture::kExample}};
    const auto created = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_EQ(201, created->status) << created->body;
    const auto id = json_body(created).at("id").get<std::string>();

    // Deliberately connected without waiting: whether this arrives during the run or after it, the
    // client's path is the same, which is what the buffered replay is for.
    std::string stream;
    const auto response = client.Get("/api/runs/" + id + "/events",
                                     [&](const char *data, std::size_t length) {
                                         stream.append(data, length);
                                         return true;
                                     });
    ASSERT_TRUE(response) << "the stream did not complete";

    EXPECT_NE(std::string::npos, stream.find("event: run_started"));
    EXPECT_NE(std::string::npos, stream.find("event: year_completed"));
    EXPECT_NE(std::string::npos, stream.find("event: run_completed"));
    EXPECT_NE(std::string::npos, stream.find("event: state"));

    // Every data line is a JSON object carrying its own type, so a client that missed the
    // `event:` line still knows what it has.
    std::istringstream lines{stream};
    std::string line;
    std::size_t parsed = 0;
    while (std::getline(lines, line)) {
        if (!line.starts_with("data: ")) {
            continue;
        }
        const auto payload = nlohmann::json::parse(line.substr(6));
        EXPECT_TRUE(payload.contains("type")) << line;
        ++parsed;
    }
    EXPECT_GT(parsed, 3U);

    ASSERT_EQ("completed", served.wait_for_run(id).value("state", std::string{}));
}

TEST(ServerApi, TheStreamForARunThatIsNotThereIsA404) {
    ServedFixture served{"api_events_missing"};
    auto client = served.client();
    const auto response = client.Get("/api/runs/nope/events");
    ASSERT_TRUE(response);
    EXPECT_EQ(404, response->status);
}

// --- the boundary that makes "no auth" safe --------------------------------------------------------

TEST(ServerApi, ANonLoopbackHostIsRefusedWithAReason) {
    for (const auto *host : {"0.0.0.0", "192.168.1.10", "example.com", "::"}) {
        const auto refusal = hgps::server::loopback_refusal(host);
        EXPECT_FALSE(refusal.empty()) << host << " was accepted";
        EXPECT_NE(std::string::npos, refusal.find(host));
        EXPECT_NE(std::string::npos, refusal.find("authentication"))
            << "the refusal should say why, not just that: " << refusal;
    }
    for (const auto *host : {"127.0.0.1", "::1", "localhost"}) {
        EXPECT_TRUE(hgps::server::loopback_refusal(host).empty()) << host << " was refused";
    }
}
