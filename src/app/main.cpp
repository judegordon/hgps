// healthgps — the command-line host.
//
// A client of hgps::engine and nothing more: it parses arguments, subscribes to the engine's event
// stream, prints, and chooses an exit code. Every decision about what a run *is* lives in the
// library, because a second host would otherwise have to make the same decisions again
// (docs/api.md, docs/decisions/0032-library-and-a-thin-cli.md).
//
// This file may include nothing outside include/hgps/ and src/app/. `tests/app/cli_boundary_test.cpp`
// checks that, because a boundary nobody checks is a boundary that leaks.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Console/program.cpp.
#include "options.h"
#include "reporter.h"

#include "hgps/engine.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr const char *kProgramName = "healthgps";

/// @brief Exit codes, so a script can tell the kinds of failure apart.
enum class ExitCode : int {
    success = 0,
    usage = 2,
    input_problem = 3,
    cancelled = 4,
    internal_error = 70,
};

int to_int(ExitCode code) { return static_cast<int>(code); }

/// @brief Prints a report if it has anything in it.
void print(const hgps::api::Report &report) {
    if (!report.empty()) {
        std::cerr << report.to_string();
    }
}

} // namespace

int main(int argc, char **argv) {
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    auto parsed = hgps::app::parse_options(arguments);
    if (!parsed.options.has_value()) {
        std::cerr << kProgramName << ": " << parsed.message << "\n\n"
                  << hgps::app::usage_text();
        return to_int(ExitCode::usage);
    }

    const auto &options = *parsed.options;

    if (options.help) {
        std::cout << hgps::app::usage_text();
        return to_int(ExitCode::success);
    }
    if (options.version) {
        std::cout << hgps::app::version_text(kProgramName);
        return to_int(ExitCode::success);
    }

    try {
        hgps::api::Report report;

        hgps::api::LoadOptions load_options;
        load_options.output_folder = options.output_folder;
        load_options.job_id = options.job_id;
        load_options.verbose = options.verbose;

        const auto configuration =
            hgps::api::load_configuration(options.config, load_options, report);
        if (!configuration.has_value()) {
            print(report);
            std::cerr << kProgramName << ": the configuration has errors; nothing was run.\n";
            return to_int(ExitCode::input_problem);
        }

        const auto data = hgps::api::resolve_data(*configuration, report);
        if (!data.has_value()) {
            print(report);
            return to_int(ExitCode::input_problem);
        }

        auto run = hgps::api::build_run(*configuration, *data, report);
        if (!run.has_value()) {
            print(report);
            return to_int(ExitCode::input_problem);
        }

        // Warnings are worth seeing even on a successful run, and they are worth seeing before the
        // run rather than after it.
        print(report);
        report.clear();

        if (options.dry_run) {
            std::cout << hgps::app::dry_run_text(kProgramName, run->description());
            return to_int(ExitCode::success);
        }

        hgps::api::RunOptions run_options;
        run_options.threads = options.threads;

        hgps::app::ConsoleReporter reporter{std::cout, std::cerr, options.progress};
        const hgps::api::CancellationToken cancellation;

        const auto summary =
            hgps::api::execute(*run, run_options, &reporter, cancellation, report);
        print(report);

        if (!summary.succeeded) {
            return to_int(ExitCode::internal_error);
        }
        return to_int(summary.cancelled ? ExitCode::cancelled : ExitCode::success);
    } catch (const std::exception &error) {
        // A broken invariant in the engine, or a file it could not write. Either way it is not the
        // user's input: that comes back as a diagnostic, never as an exception.
        std::cerr << kProgramName << ": " << error.what() << '\n';
        return to_int(ExitCode::internal_error);
    }
}
