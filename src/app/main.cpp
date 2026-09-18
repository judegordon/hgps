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
#include "serve_options.h"
#include "server.h"

#include "hgps/engine.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
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

/// @brief `healthgps serve …` — the local JSON server (docs/server-api.md).
///
/// Recognised only as the *first* argument, so no existing command line changes meaning. A test
/// asserts that.
int serve(const std::vector<std::string> &arguments) {
    auto parsed = hgps::app::parse_serve_options(arguments);
    if (!parsed.options.has_value()) {
        std::cerr << kProgramName << " serve: " << parsed.message << "\n\n"
                  << hgps::app::serve_usage_text();
        return to_int(ExitCode::usage);
    }
    const auto &options = *parsed.options;
    if (options.help) {
        std::cout << hgps::app::serve_usage_text();
        return to_int(ExitCode::success);
    }

    if (const auto refusal = hgps::server::loopback_refusal(options.host); !refusal.empty()) {
        std::cerr << kProgramName << " serve: " << refusal << "\n";
        return to_int(ExitCode::usage);
    }

    hgps::server::Options server_options;
    server_options.host = options.host;
    server_options.port = options.port;
    server_options.config_roots = options.config_roots;
    server_options.runs_root = options.runs_root;
    server_options.web_root = options.web_root;
    server_options.schema_path = options.schema_path;

    hgps::server::Server server{server_options};
    const auto port = server.start();
    if (port == 0) {
        return to_int(ExitCode::input_problem);
    }

    std::cout << "hgps serve: http://" << options.host << ":" << port << "\n";
    if (!options.web_root.empty()) {
        std::cout << "  web:     " << options.web_root.string() << "\n";
    }
    for (const auto &root : options.config_roots) {
        std::cout << "  configs: " << root.string() << "\n";
    }
    std::cout << "  runs:    " << options.runs_root.string() << "\n";
    std::cout << "Ctrl-C to stop.\n" << std::flush;

    // Waits for a signal, not for stdin. Reading stdin was the first thing tried and it is wrong:
    // a server started with `< /dev/null`, under nohup, or by anything that is not a terminal sees
    // EOF at once and exits before it has served a request.
    //
    // The handler does nothing but set a flag, because almost nothing else is safe to do in one.
    static std::atomic<bool> stopping{false};
    const auto handler = [](int) { stopping.store(true, std::memory_order_relaxed); };
    std::signal(SIGINT, handler);
    std::signal(SIGTERM, handler);

    while (!stopping.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    std::cout << "\nhgps serve: stopping\n" << std::flush;
    server.stop();
    return to_int(ExitCode::success);
}

/// @brief Prints a report if it has anything in it.
void print(const hgps::api::Report &report) {
    if (!report.empty()) {
        std::cerr << report.to_string();
    }
}

} // namespace

int main(int argc, char **argv) {
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    if (!arguments.empty() && arguments.front() == "serve") {
        try {
            return serve({arguments.begin() + 1, arguments.end()});
        } catch (const std::exception &failure) {
            std::cerr << kProgramName << " serve: " << failure.what() << "\n";
            return to_int(ExitCode::internal_error);
        }
    }

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
        load_options.baseline_compat = options.baseline_compat;

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
        run_options.perturbation = options.perturb;

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
