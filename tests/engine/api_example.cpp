// The usage example in docs/api.md lives here, between the two markers, so that it is compiled and
// run by the test suite instead of being prose that drifts. `ApiExample.TheDocumentQuotesThisFile`
// checks that the document's code block and the marked region below are the same text.
#include "api_example.h"

// --- docs/api.md example: begin ---
#include "hgps/engine.h"

#include <iostream>

namespace hgps::example {

/// Prints a line as each simulated year finishes.
class YearPrinter final : public api::EventSubscriber {
  public:
    void on_year_completed(const api::YearCompleted &event) override {
        std::cout << event.scenario << ' ' << event.year << ": " << event.population_size
                  << " people, " << event.elapsed_ms << " ms\n";
    }
};

/// Runs one configuration. Returns 0 on success, 3 for a problem with the inputs, 70 otherwise.
int run_one(const std::filesystem::path &config_path) {
    api::Report report;

    // Step 1: the configuration. Everything wrong with it is reported together.
    const auto configuration =
        api::load_configuration(config_path, api::LoadOptions{}, report);
    if (!configuration.has_value()) {
        std::cerr << report.to_string();
        return 3;
    }

    // Step 2: the data. Downloads and verifies if the source is an archive or a URL.
    const auto data = api::resolve_data(*configuration, report);
    if (!data.has_value()) {
        std::cerr << report.to_string();
        return 3;
    }

    // Step 3: the run. Reads every model file; this is where most input mistakes are found.
    auto run = api::build_run(*configuration, *data, report);
    if (!run.has_value()) {
        std::cerr << report.to_string();
        return 3;
    }

    // Warnings are worth seeing even when everything loaded.
    std::cerr << report.to_string();
    std::cout << run->description().cohort_size << " people, " << run->description().start_time
              << "–" << run->description().stop_time << ", seed " << run->description().seed
              << '\n';

    // Step 4: run it. The token is never cancelled here; call cancel() on a copy from any thread
    // and the run stops at the end of its current year.
    YearPrinter printer;
    const api::CancellationToken cancellation;
    const auto summary = api::execute(*run, api::RunOptions{}, &printer, cancellation, report);

    for (const auto &path : summary.outputs) {
        std::cout << path.string() << '\n';
    }
    return summary.succeeded ? 0 : 70;
}

} // namespace hgps::example
// --- docs/api.md example: end ---
