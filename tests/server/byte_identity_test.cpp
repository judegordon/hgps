// A run started through the server produces byte-identical output to the same run from the CLI.
//
// This is the server's correctness test, and it is the only one that matters at this level. Every
// other test here checks that an endpoint says the right thing; this one checks that the endpoint
// did not quietly change what the engine does. The determinism contract says the same config, seed,
// data and binary produce byte-identical CSV output (docs/design.md section 4) — if routing a run
// through HTTP breaks that, the server is not a host of this engine, it is a fork of it.
//
// What it does NOT assert is that the manifests match: a manifest records when the run happened and
// how long it took, and two runs a second apart must differ there. That is the same rule the
// manifest's own test follows.
#include "support.h"

#include "hgps/engine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// @brief The first place two strings differ, for a failure message worth reading.
std::string first_difference(const std::string &left, const std::string &right) {
    const auto shared = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < shared; ++i) {
        if (left[i] != right[i]) {
            const auto from = i > 60 ? i - 60 : std::size_t{0};
            return "byte " + std::to_string(i) + "\n  left:  ..." +
                   left.substr(from, 140) + "\n  right: ..." + right.substr(from, 140);
        }
    }
    return left.size() == right.size()
               ? std::string{"nowhere"}
               : "length: " + std::to_string(left.size()) + " against " +
                     std::to_string(right.size());
}

/// @brief The CSV a finished run wrote, by the name the configuration gave it.
std::filesystem::path result_csv_of(const std::filesystem::path &runs, const std::string &id,
                                    const nlohmann::json &finished) {
    for (const auto &name : finished.at("results")) {
        const auto text = name.get<std::string>();
        if (text.ends_with(".csv")) {
            return runs / id / text;
        }
    }
    return {};
}

/// Both synthetic packs: the second one's output is not called `result.csv` and is not the same
/// name twice, which is the shape a real configuration has (tests/support/fixture_packs.h).
class ServerByteIdentity : public hgps::test::ServedPackTest {};

HGPS_TEST_EVERY_FIXTURE_PACK(ServerByteIdentity);

} // namespace

TEST_P(ServerByteIdentity, ARunStartedOverHttpIsByteIdenticalToTheSameRunInProcess) {
    hgps::test::ServedFixture served{"identity"};
    auto client = served.client();

    // Through the server.
    const nlohmann::json body{{"example", example()}};
    const auto created = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_TRUE(created);
    ASSERT_EQ(201, created->status) << created->body;
    const auto id = json_body_of(created).at("id").get<std::string>();
    const auto finished = served.wait_for_run(id);
    ASSERT_EQ("completed", finished.value("state", std::string{})) << finished.dump(2);

    const auto served_csv = result_csv_of(served.runs(), id, finished);
    ASSERT_TRUE(std::filesystem::is_regular_file(served_csv))
        << served_csv << ", from " << finished.at("results").dump();

    // The same configuration, through the public API — which is exactly what the CLI does, four
    // calls of it, and is why `run_simulation` exists in tests/support (ADR 0032).
    const auto direct_folder = pack_scratch("identity_direct");
    const auto config = served.configs() / example() / "config.json";
    const auto direct = hgps::test::run_simulation(config, direct_folder);
    ASSERT_TRUE(direct.succeeded) << direct.report.to_string();

    const auto over_http = read_file(served_csv);
    const auto in_process = read_file(direct.csv_path);

    ASSERT_FALSE(over_http.empty());
    EXPECT_EQ(over_http, in_process)
        << "a run routed through HTTP produced different numbers; first difference at "
        << first_difference(over_http, in_process);
}

TEST_P(ServerByteIdentity, TheDownloadedCsvIsTheFileOnDiskAndNotAReEncodingOfIt) {
    // The download endpoint reads and writes bytes. A transcoding — a newline translation, a
    // trailing byte lost — would make every downloaded result subtly different from the one the
    // engine wrote, and nothing else here would notice.
    hgps::test::ServedFixture served{"identity_download"};
    auto client = served.client();

    const nlohmann::json body{{"example", example()}};
    const auto created = client.Post("/api/runs", body.dump(), "application/json");
    ASSERT_EQ(201, created->status) << created->body;
    const auto id = json_body_of(created).at("id").get<std::string>();
    const auto finished = served.wait_for_run(id);
    ASSERT_EQ("completed", finished.value("state", std::string{}));

    const auto path = result_csv_of(served.runs(), id, finished);
    ASSERT_FALSE(path.empty()) << finished.at("results").dump();

    const auto downloaded =
        client.Get("/api/runs/" + id + "/results/" + path.filename().string());
    ASSERT_TRUE(downloaded);
    ASSERT_EQ(200, downloaded->status);

    const auto on_disk = read_file(path);
    ASSERT_FALSE(on_disk.empty());
    EXPECT_EQ(on_disk, downloaded->body)
        << "the download differs from the file; first difference at "
        << first_difference(on_disk, downloaded->body);
}

TEST_P(ServerByteIdentity, TwoRunsOverHttpOfTheSameConfigAgreeByteForByte) {
    // The determinism contract, through the server rather than around it. The engine asserts this
    // of itself; this asserts that the server's per-run output folder, its thread and its
    // subscriber have not introduced a difference.
    hgps::test::ServedFixture served{"identity_repeat"};
    auto client = served.client();

    std::vector<std::string> outputs;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const nlohmann::json body{{"example", example()},
                                  {"threads", attempt == 0 ? 1 : 4}};
        const auto created = client.Post("/api/runs", body.dump(), "application/json");
        ASSERT_TRUE(created);
        ASSERT_EQ(201, created->status) << created->body;
        const auto id = json_body_of(created).at("id").get<std::string>();
        const auto finished = served.wait_for_run(id);
        ASSERT_EQ("completed", finished.value("state", std::string{})) << "attempt " << attempt;
        outputs.push_back(read_file(result_csv_of(served.runs(), id, finished)));
        ASSERT_FALSE(outputs.back().empty()) << "attempt " << attempt;
    }

    EXPECT_EQ(outputs[0], outputs[1])
        << "two runs of one config over HTTP, at one thread and at four, differ at "
        << first_difference(outputs[0], outputs[1]);
}
