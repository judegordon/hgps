#include "options.h"

#include <charconv>

#include <fmt/format.h>

namespace hgps::app {
namespace {

std::optional<std::size_t> parse_size(const std::string &text) {
    std::size_t value = 0;
    const auto *begin = text.data();
    const auto *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::string usage_text() {
    return R"(healthgps — a deterministic Health-GPS microsimulation

Usage:
  healthgps --config FILE [options]

Options:
  -c, --config FILE     The config v2 file to run. Required.
                        tools/convert-config turns an upstream config into one.
  -o, --output DIR      The output folder, when the config leaves output.folder empty.
      --dry-run         Validate the config, the models, the data index and the disease
                        registry, then stop. Reports every problem it finds, not just the first.
      --progress        Print a line as each simulated year finishes, on stderr.
      --perturb SPEC    Test-only. Corrupts named output channels — 'mean_bmi=scale:1.01', say —
                        so that the equivalence harness can check it fails where it should. The
                        run's manifest records what was set, and a specification naming a channel
                        the output does not have fails the run. Never use it for analysis.
  -j, --jobid N         An HPC array job identifier. Appended to the output file name unless the
                        name contains {JOBID}.
  -T, --threads N       Workers for the RNG-free parallel sections (default 1). The output is
                        byte-identical at any thread count; if it is not, that is a bug.
  -v, --verbose         Report more about what is happening.
      --version         Print the version and exit.
  -h, --help            Print this and exit.

Every run writes a manifest JSON beside its results recording the config hash, the data
checksum, the seed used, the engine version and commit, the host platform and the scenarios run.

The same config, seed, data and binary produce byte-identical CSV output on every run. See
docs/design.md section 4 for the contract and how it is enforced. The engine is a library —
hgps::engine, docs/api.md — and this program is a client of it.
)";
}

OptionsResult parse_options(const std::vector<std::string> &arguments) {
    Options options;

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto &argument = arguments[i];

        const auto next_value = [&](const std::string &name) -> std::optional<std::string> {
            if (i + 1 >= arguments.size()) {
                return std::nullopt;
            }
            (void)name;
            return arguments[++i];
        };

        if (argument == "-h" || argument == "--help") {
            options.help = true;
            return OptionsResult{.options = options, .message = ""};
        }

        if (argument == "--version") {
            options.version = true;
            return OptionsResult{.options = options, .message = ""};
        }

        if (argument == "-c" || argument == "--config") {
            const auto value = next_value(argument);
            if (!value.has_value()) {
                return OptionsResult{.options = std::nullopt,
                                     .message = fmt::format("{} needs a file path", argument)};
            }
            options.config = *value;
            continue;
        }

        if (argument == "-o" || argument == "--output") {
            const auto value = next_value(argument);
            if (!value.has_value()) {
                return OptionsResult{.options = std::nullopt,
                                     .message = fmt::format("{} needs a directory", argument)};
            }
            options.output_folder = *value;
            continue;
        }

        if (argument == "--dry-run") {
            options.dry_run = true;
            continue;
        }

        if (argument == "--perturb") {
            const auto value = next_value(argument);
            if (!value.has_value()) {
                return OptionsResult{.options = std::nullopt,
                                     .message = fmt::format("{} needs a specification", argument)};
            }
            options.perturb = *value;
            continue;
        }

        if (argument == "--progress") {
            options.progress = true;
            continue;
        }

        if (argument == "-v" || argument == "--verbose") {
            options.verbose = true;
            continue;
        }

        if (argument == "-j" || argument == "--jobid") {
            const auto value = next_value(argument);
            const auto parsed = value.has_value() ? parse_size(*value) : std::nullopt;
            if (!parsed.has_value()) {
                return OptionsResult{.options = std::nullopt,
                                     .message = fmt::format("{} needs a non-negative number",
                                                            argument)};
            }
            options.job_id = static_cast<int>(*parsed);
            continue;
        }

        if (argument == "-T" || argument == "--threads") {
            const auto value = next_value(argument);
            const auto parsed = value.has_value() ? parse_size(*value) : std::nullopt;
            if (!parsed.has_value() || *parsed == 0) {
                return OptionsResult{.options = std::nullopt,
                                     .message = fmt::format("{} needs a number of at least 1",
                                                            argument)};
            }
            options.threads = *parsed;
            continue;
        }

        return OptionsResult{.options = std::nullopt,
                             .message = fmt::format("unknown argument '{}'", argument)};
    }

    if (options.config.empty()) {
        return OptionsResult{.options = std::nullopt, .message = "--config FILE is required"};
    }

    return OptionsResult{.options = options, .message = ""};
}

} // namespace hgps::app
