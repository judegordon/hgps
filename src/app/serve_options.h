// `hgps serve` — the arguments the local server takes.
//
// Kept apart from `Options` because they share nothing: a run takes a config and a thread count, a
// server takes a port and some roots. One struct with two disjoint halves would be a struct whose
// fields are half meaningless whichever subcommand you are in.
//
// docs/server-api.md is the API this serves;
// docs/decisions/0042-a-local-server-in-the-same-binary.md says why it is a subcommand of this
// binary rather than a second one.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hgps::app {

/// @brief What `hgps serve` was asked for.
struct ServeOptions {
    /// @brief Loopback only. Anything else is refused before the socket opens.
    std::string host{"127.0.0.1"};

    /// @brief 0 asks the operating system for a free port.
    std::uint16_t port{8080};

    /// @brief Where to look for configurations. Repeatable; defaults to ./examples if it exists.
    std::vector<std::filesystem::path> config_roots;

    /// @brief Where runs are written and found. Defaults to ./hgps-runs.
    std::filesystem::path runs_root;

    /// @brief The built frontend, served as static files. Empty serves none.
    std::filesystem::path web_root;

    /// @brief The published config schema for `GET /api/schema`. Defaults to ./schemas/v2/config.json.
    std::filesystem::path schema_path;

    bool help{false};
};

struct ServeOptionsResult {
    std::optional<ServeOptions> options;
    std::string message;
};

/// @brief Parses the arguments after `serve`.
///
/// Defaults that name a path are filled in only when the path exists, so a server started in a
/// directory that is not this repository does not claim to serve things that are not there.
ServeOptionsResult parse_serve_options(const std::vector<std::string> &arguments);

/// @brief The help text for the subcommand.
std::string serve_usage_text();

} // namespace hgps::app
