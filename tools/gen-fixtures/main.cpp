// gen-fixtures — writes the synthetic data pack used by the tests and by offline runs.
// docs/decisions/0011-data-fetched-not-vendored.md explains why this exists.
#include "model_pack.h"
#include "pack.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout << R"(gen-fixtures — write the synthetic Health-GPS data pack

Usage:
  gen-fixtures --output DIR [--first-year Y] [--last-year Y] [--max-age N]

Writes DIR/data (a data store in the upstream layout) and DIR/model (a runnable config v2 with
its model definitions, FactorsMean tables and input dataset).

Every number written is invented: closed-form functions of age, year and sex, with no random
component. The output is byte-identical on every run. The pack carries a SYNTHETIC.md saying so.
It exists so that tests and CI can run without the network and without the real, restrictively
licensed disease data. It must never be used for analysis.
)";
}

} // namespace

int main(int argc, char **argv) {
    std::filesystem::path output;
    hgps::tools::FixturePackSpec spec;

    const std::vector<std::string> arguments(argv + 1, argv + argc);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto &argument = arguments[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= arguments.size()) {
                throw std::runtime_error("missing value for " + argument);
            }
            return arguments[++i];
        };

        try {
            if (argument == "--output" || argument == "-o") {
                output = next();
            } else if (argument == "--first-year") {
                spec.first_year = std::stoi(next());
            } else if (argument == "--last-year") {
                spec.last_year = std::stoi(next());
            } else if (argument == "--max-age") {
                spec.max_age = std::stoi(next());
            } else if (argument == "--help" || argument == "-h") {
                print_usage();
                return EXIT_SUCCESS;
            } else {
                std::cerr << "gen-fixtures: unknown argument '" << argument << "'\n";
                return EXIT_FAILURE;
            }
        } catch (const std::exception &error) {
            std::cerr << "gen-fixtures: " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    if (output.empty()) {
        std::cerr << "gen-fixtures: --output DIR is required\n";
        print_usage();
        return EXIT_FAILURE;
    }

    if (spec.last_year <= spec.first_year || spec.max_age < 1) {
        std::cerr << "gen-fixtures: the horizon must be at least two years and max-age at least 1\n";
        return EXIT_FAILURE;
    }

    try {
        const auto data_files = hgps::tools::write_fixture_pack(output / "data", spec);
        const auto model_files = hgps::tools::write_model_pack(output / "model", spec);
        std::cout << "gen-fixtures: wrote " << data_files << " data files and " << model_files
                  << " model files to " << output.string() << '\n';
    } catch (const std::exception &error) {
        std::cerr << "gen-fixtures: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
