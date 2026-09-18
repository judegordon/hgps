#include "serve_options.h"

#include <charconv>
#include <string_view>

#include <fmt/format.h>

namespace hgps::app {
namespace {

std::optional<std::uint16_t> parse_port(const std::string &text) {
    unsigned long value = 0;
    const auto *begin = text.data();
    const auto *end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || stop != end || value > 65535U) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

/// @brief A default path, used only if it is there.
std::filesystem::path existing(const std::filesystem::path &candidate) {
    std::error_code error;
    return std::filesystem::exists(candidate, error) ? candidate : std::filesystem::path{};
}

} // namespace

std::string serve_usage_text() {
    return R"(healthgps serve — the engine over HTTP, on localhost

Usage:
  healthgps serve [options]

Options:
      --host HOST       127.0.0.1 (the default), ::1 or localhost. Anything else is refused
                        before the socket opens: there is no authentication, and that is only
                        safe because the server cannot be reached from another machine.
      --port N          The port (default 8080). 0 asks the operating system for a free one.
      --configs DIR     Where to look for configurations. Repeatable. Defaults to ./examples
                        when that directory exists.
      --runs DIR        Where runs are written and found (default ./hgps-runs). A completed run
                        is its manifest, so this directory is the whole of the run history and
                        survives a restart.
      --web DIR         The built frontend, served as static files. With it, the graphical host
                        is this binary plus one folder.
      --schema FILE     The config schema to publish at /api/schema
                        (default ./schemas/v2/config.json when it exists).
  -h, --help            Print this and exit.

The API is documented in docs/server-api.md. One run happens at a time, and a second is refused
rather than queued, because the engine's contract says two simulations must not overlap in one
process.
)";
}

ServeOptionsResult parse_serve_options(const std::vector<std::string> &arguments) {
    ServeOptions options;

    const auto next = [&arguments](std::size_t &i) -> std::optional<std::string> {
        if (i + 1 >= arguments.size()) {
            return std::nullopt;
        }
        return arguments[++i];
    };

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto &argument = arguments[i];

        if (argument == "-h" || argument == "--help") {
            options.help = true;
            continue;
        }
        if (argument == "--host") {
            const auto value = next(i);
            if (!value.has_value()) {
                return {std::nullopt, "--host needs an address"};
            }
            options.host = *value;
            continue;
        }
        if (argument == "--port") {
            const auto value = next(i);
            const auto parsed = value.has_value() ? parse_port(*value) : std::nullopt;
            if (!parsed.has_value()) {
                return {std::nullopt, "--port needs a number from 0 to 65535"};
            }
            options.port = *parsed;
            continue;
        }
        if (argument == "--configs") {
            const auto value = next(i);
            if (!value.has_value()) {
                return {std::nullopt, "--configs needs a directory"};
            }
            options.config_roots.emplace_back(*value);
            continue;
        }
        if (argument == "--runs") {
            const auto value = next(i);
            if (!value.has_value()) {
                return {std::nullopt, "--runs needs a directory"};
            }
            options.runs_root = *value;
            continue;
        }
        if (argument == "--web") {
            const auto value = next(i);
            if (!value.has_value()) {
                return {std::nullopt, "--web needs a directory"};
            }
            options.web_root = *value;
            continue;
        }
        if (argument == "--schema") {
            const auto value = next(i);
            if (!value.has_value()) {
                return {std::nullopt, "--schema needs a file"};
            }
            options.schema_path = *value;
            continue;
        }

        return {std::nullopt, fmt::format("unknown option '{}'", argument)};
    }

    if (options.config_roots.empty()) {
        if (const auto examples = existing("examples"); !examples.empty()) {
            options.config_roots.push_back(examples);
        }
    }
    if (options.runs_root.empty()) {
        options.runs_root = "hgps-runs";
    }
    if (options.schema_path.empty()) {
        options.schema_path = existing(std::filesystem::path{"schemas"} / "v2" / "config.json");
    }

    return {options, {}};
}

} // namespace hgps::app
