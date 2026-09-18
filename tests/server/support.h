// A server on a free port, with a client, for the life of a test.
#pragma once

#include "server.h"

#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace hgps::test {

/// @brief A running server and a client pointed at it.
///
/// Port 0, so two tests never fight over a fixed port, and torn down in the destructor so a failing
/// test does not leave a socket behind.
class ServedFixture {
  public:
    /// @brief The example id the default configs root offers.
    ///
    /// The synthetic pack keeps its config in `model/` and its tables in `data/` beside it, and
    /// the config's `data.source` is the relative `../data`. The server's example list is "a
    /// directory holding a config.json", so the pack is laid out under a root of its own as
    /// `<root>/Synthetic/config.json` and `<root>/data/`, which keeps that relative path meaning
    /// what it meant. `data/` has no config.json, so it is not listed as an example.
    ///
    /// Copied rather than linked, deliberately: the server resolves symlinks before checking a
    /// path is inside its roots, so a link out of a root is refused — which is the rule working.
    static constexpr const char *kExample = "Synthetic";

    explicit ServedFixture(const std::string &name,
                           std::vector<std::filesystem::path> config_roots = {},
                           std::filesystem::path web_root = {}) {
        runs_ = scratch_dir(name + "_runs");
        configs_ = scratch_dir(name + "_configs");
        const auto recursive = std::filesystem::copy_options::recursive |
                               std::filesystem::copy_options::overwrite_existing;
        std::filesystem::copy(synthetic_model_dir(), configs_ / kExample, recursive);
        std::filesystem::copy(synthetic_data_dir(), configs_ / "data", recursive);

        server::Options options;
        options.host = "127.0.0.1";
        options.port = 0;
        options.config_roots = config_roots.empty()
                                   ? std::vector<std::filesystem::path>{configs_}
                                   : std::move(config_roots);
        options.runs_root = runs_;
        options.web_root = std::move(web_root);
        options.schema_path = schemas_dir() / "v2" / "config.json";

        server_ = std::make_unique<server::Server>(std::move(options));
        port_ = server_->start();
    }

    ~ServedFixture() {
        if (server_ != nullptr) {
            server_->stop();
        }
    }

    ServedFixture(const ServedFixture &) = delete;
    ServedFixture &operator=(const ServedFixture &) = delete;
    ServedFixture(ServedFixture &&) = delete;
    ServedFixture &operator=(ServedFixture &&) = delete;

    std::uint16_t port() const noexcept { return port_; }
    const std::filesystem::path &runs() const noexcept { return runs_; }
    const std::filesystem::path &configs() const noexcept { return configs_; }

    httplib::Client client() const {
        httplib::Client client{"127.0.0.1", port_};
        client.set_read_timeout(std::chrono::seconds{120});
        client.set_write_timeout(std::chrono::seconds{30});
        return client;
    }

    /// @brief A GET whose body is parsed as JSON. Fails the caller's expectation if it is not.
    static nlohmann::json json_of(const httplib::Result &result) {
        if (!result) {
            return nlohmann::json::object();
        }
        try {
            return nlohmann::json::parse(result->body);
        } catch (const nlohmann::json::exception &) {
            return nlohmann::json::object();
        }
    }

    /// @brief Waits until a run reaches a terminal state, or gives up.
    ///
    /// Polls `GET /api/runs/{id}` rather than reading the record directly, because what a test
    /// should check is what a client would see.
    nlohmann::json wait_for_run(const std::string &id,
                                std::chrono::seconds limit = std::chrono::seconds{120}) const {
        auto http = client();
        const auto deadline = std::chrono::steady_clock::now() + limit;
        nlohmann::json last;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto response = http.Get("/api/runs/" + id);
            last = json_of(response);
            const auto state = last.value("state", std::string{});
            if (state == "completed" || state == "cancelled" || state == "failed") {
                return last;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
        }
        return last;
    }

  private:
    std::filesystem::path runs_;
    std::filesystem::path configs_;
    std::unique_ptr<server::Server> server_;
    std::uint16_t port_{0};
};

} // namespace hgps::test

/// @brief The body of a response, parsed as JSON. A free function so a test file need not name the
///        fixture to use it.
inline nlohmann::json json_body_of(const httplib::Result &result) {
    return hgps::test::ServedFixture::json_of(result);
}
