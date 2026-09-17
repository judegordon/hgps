// convert-config — upstream Health-GPS config v1 to this repository's config v2.
// docs/decisions/0010-config-v2-and-a-converter.md.
#include "config/convert.h"

#include "config/loader.h"
#include "diagnostics/issue_report.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout << R"(convert-config — upstream Health-GPS config v1 to config v2

Usage:
  convert-config --input FILE --output FILE [--policy-scenario S1] [--check]

Options:
  -i, --input FILE   The upstream config to read. Both variants work: a legacy config.json or a
                     new_config.json.
  -o, --output FILE  Where to write the converted config v2.
  -r, --rebase       Rewrite the config's relative input paths so they still name the same files
                     from the output's directory. Use this when the converted config lives
                     somewhere other than beside the model CSVs and JSONs it names.
      --policy-scenario S1..S7
                     Which of the seven modelled policy scenarios to take the policy covariance
                     and policy-effect files from. Only KevinHall_FINCH needs it: its
                     static_model.json names two files that the data pack does not contain,
                     shipping S1_… to S7_… variants instead (audit D-02). The converter writes a
                     patched copy of the static model beside the output and points the config at
                     it. Default: S1, which is what the pack's own new_static_model.json uses.
      --check        Also load the result with this build's config loader and report what it
                     says. Recommended: a conversion that does not load is not a conversion.
  -h, --help         Print this and exit.

Every change is reported. Where the input has no project_requirements block, the converter takes
it from a new_config.json beside the input if there is one, and otherwise derives what it can and
fills the rest from the documented defaults — saying which route each part took. Read the result
before trusting it: upstream gates most current behaviour on that block and no upstream
config.json carries it.
)";
}

} // namespace

int main(int argc, char **argv) {
    std::filesystem::path input;
    std::filesystem::path output;
    bool check = false;
    bool rebase = false;
    std::string policy_scenario = "S1";

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
            if (argument == "-i" || argument == "--input") {
                input = next();
            } else if (argument == "-o" || argument == "--output") {
                output = next();
            } else if (argument == "-r" || argument == "--rebase") {
                rebase = true;
            } else if (argument == "--policy-scenario") {
                policy_scenario = next();
            } else if (argument == "--check") {
                check = true;
            } else if (argument == "-h" || argument == "--help") {
                print_usage();
                return EXIT_SUCCESS;
            } else {
                std::cerr << "convert-config: unknown argument '" << argument << "'\n";
                return EXIT_FAILURE;
            }
        } catch (const std::exception &error) {
            std::cerr << "convert-config: " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    if (input.empty() || output.empty()) {
        std::cerr << "convert-config: --input FILE and --output FILE are both required\n\n";
        print_usage();
        return EXIT_FAILURE;
    }

    nlohmann::json document;
    try {
        std::ifstream stream{input};
        if (!stream) {
            std::cerr << "convert-config: cannot open " << input.string() << '\n';
            return EXIT_FAILURE;
        }
        document = nlohmann::json::parse(stream);
    } catch (const nlohmann::json::parse_error &error) {
        std::cerr << "convert-config: " << input.string() << " is not valid JSON: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }

    auto result = hgps::config::convert_config(document, input);

    if (rebase && result.document.has_value()) {
        auto output_directory = output.parent_path();
        if (output_directory.empty()) {
            output_directory = ".";
        }
        hgps::config::rebase_input_paths(*result.document, input.parent_path(), output_directory,
                                        result.notes);
    }

    // The policy-scenario patch happens after rebasing, so the patched copy's own paths are
    // already absolute and it can live anywhere.
    if (result.document.has_value()) {
        const auto &models = (*result.document)["modelling"]["risk_factor_models"];
        if (models.contains("static") && models["static"].is_string()) {
            const std::filesystem::path named{models["static"].get<std::string>()};
            auto output_directory = output.parent_path();
            if (output_directory.empty()) {
                output_directory = ".";
            }
            const auto absolute_model =
                named.is_absolute() ? named : std::filesystem::weakly_canonical(
                                                  output_directory / named);
            hgps::config::apply_policy_scenario(*result.document, absolute_model, policy_scenario,
                                                output_directory, result.notes);
        }
    }

    for (const auto &note : result.notes) {
        const auto *label = note.level == hgps::config::ConversionNote::Level::error
                                ? "error"
                                : note.level == hgps::config::ConversionNote::Level::warning
                                      ? "warning"
                                      : "note";
        std::cerr << label << ": " << note.message << '\n';
    }

    if (!result.succeeded()) {
        std::cerr << "convert-config: the input could not be converted; nothing was written.\n";
        return EXIT_FAILURE;
    }

    std::filesystem::create_directories(output.parent_path());
    {
        std::ofstream stream{output, std::ios::trunc};
        if (!stream) {
            std::cerr << "convert-config: cannot write " << output.string() << '\n';
            return EXIT_FAILURE;
        }
        stream << result.document->dump(2) << '\n';
    }

    std::cout << "convert-config: wrote " << output.string() << '\n';

    if (check) {
        hgps::diag::IssueReport report;
        const auto loaded = hgps::config::load(output, hgps::config::LoadOptions{}, report);
        if (!report.empty()) {
            std::cerr << report.to_string();
        }

        if (!loaded.has_value()) {
            std::cerr << "convert-config: the converted config does not load. The conversion is "
                         "mechanical; the errors above are things only you can decide.\n";
            return EXIT_FAILURE;
        }

        std::cout << "convert-config: the converted config loads cleanly.\n";
    }

    return EXIT_SUCCESS;
}
