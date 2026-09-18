// `hgps serve`: the engine over HTTP, on localhost, in the binary that already links it.
//
// docs/server-api.md is the contract; docs/decisions/0042-a-local-server-in-the-same-binary.md says
// why it lives here, why cpp-httplib, why loopback only and why one run at a time.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace hgps::server {

/// @brief How to start the server.
struct Options {
    /// @brief Loopback only. Anything else is refused before the socket is opened, because there
    ///        is no authentication and adding one would invite binding somewhere reachable.
    std::string host{"127.0.0.1"};

    /// @brief 0 asks the operating system for a free port, which is what a test wants.
    std::uint16_t port{8080};

    /// @brief Directories to look for configurations in. The first is the default root.
    std::vector<std::filesystem::path> config_roots;

    /// @brief Where runs are written and found.
    std::filesystem::path runs_root;

    /// @brief The built frontend, served as static files. Empty serves no assets.
    std::filesystem::path web_root;

    /// @brief The published config schema, for `GET /api/schema`.
    std::filesystem::path schema_path;
};

/// @brief Why a host was refused. Empty if it is acceptable.
///
/// Separate from `Server` so that the refusal can be tested, and reported by the command line,
/// without starting anything.
std::string loopback_refusal(const std::string &host);

/// @brief The server. Construct, then `listen` — or `start` for a test that wants it in the
///        background and needs the port it actually bound.
class Server {
  public:
    explicit Server(Options options);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;
    Server(Server &&) = delete;
    Server &operator=(Server &&) = delete;

    /// @brief Binds and serves until `stop`. Blocks.
    /// @returns false if the host was refused or the port could not be bound.
    bool listen();

    /// @brief Binds, then serves on a thread of its own. Returns the port actually bound, or 0.
    std::uint16_t start();

    void stop();

    /// @brief The port in use once bound.
    std::uint16_t port() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::thread thread_;
};

} // namespace hgps::server
