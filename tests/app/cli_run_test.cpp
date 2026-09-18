// The command-line host, run as a command line: the binary, on a real configuration, in a real
// working directory.
//
// The suite had no test of this. `tests/app/options_test.cpp` parses argument vectors and
// `tests/app/cli_boundary_test.cpp` reads the CLI's includes; everything that actually ran a
// configuration went through `hgps::api` in process, with the output folder overridden by the test
// harness. So two things the CLI alone is responsible for were untested: that a configuration's own
// `output.folder` is honoured, relative to the process's working directory, and that the program
// prints the files it wrote by the names it wrote them under.
//
// Both matter more since the second synthetic pack, whose folder is nested three deep and whose
// file name carries a `{TIMESTAMP}` token (tests/support/fixture_packs.h).
#include "support/fixture_packs.h"
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

struct Invocation {
    int status{-1};
    std::string output;
};

/// @brief Runs the built `healthgps` with a working directory of its own.
///
/// A shell is used rather than `posix_spawn` because the working directory is the point: the
/// configured output folder is relative to it, and a test that ran the binary from the build tree
/// would be testing something else. The paths are the test's own scratch directories.
Invocation run_cli(const std::filesystem::path &working_directory,
                   const std::vector<std::string> &arguments) {
    std::string command = "cd '" + working_directory.string() + "' && '" +
                          std::string{HGPS_CLI_EXECUTABLE} + "'";
    for (const auto &argument : arguments) {
        command += " '" + argument + "'";
    }
    const auto log = working_directory / "cli-output.txt";
    command += " > '" + log.string() + "' 2>&1";

    Invocation result;
    result.status = std::system(command.c_str());
    std::ifstream stream{log};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    result.output = buffer.str();
    return result;
}

/// @brief Every regular file under a directory, as paths relative to it.
std::vector<std::filesystem::path> files_under(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> found;
    if (!std::filesystem::is_directory(root)) {
        return found;
    }
    for (const auto &entry : std::filesystem::recursive_directory_iterator{root}) {
        if (entry.is_regular_file()) {
            found.push_back(std::filesystem::relative(entry.path(), root));
        }
    }
    return found;
}

class CliRun : public hgps::test::FixturePackTest {
  protected:
    /// @brief The pack's own `output.folder`, as written in its configuration.
    std::string configured_folder() const {
        return hgps::test::config_document(pack()).at("output").at("folder").get<std::string>();
    }
};

HGPS_TEST_EVERY_FIXTURE_PACK(CliRun);

} // namespace

TEST_P(CliRun, WritesIntoTheFolderTheConfigurationNames) {
    const auto working = pack_scratch("cli_run");
    const auto invocation = run_cli(working, {"--config", pack().config().string()});

    ASSERT_EQ(0, invocation.status) << invocation.output;

    // Relative to the working directory, and nested as deeply as the configuration says.
    const auto folder = working / configured_folder();
    ASSERT_TRUE(std::filesystem::is_directory(folder))
        << folder << " was not created; the program said:\n"
        << invocation.output;

    std::size_t csv = 0;
    std::size_t manifests = 0;
    for (const auto &relative : files_under(folder)) {
        const auto name = relative.filename().string();
        csv += name.ends_with(".csv") ? 1U : 0U;
        manifests += name.ends_with("_manifest.json") ? 1U : 0U;
    }
    EXPECT_EQ(1U, csv) << "one result CSV, whatever it is called";
    EXPECT_EQ(1U, manifests);
}

TEST_P(CliRun, PrintsEveryFileItWroteByTheNameItWroteIt) {
    const auto working = pack_scratch("cli_run_listing");
    const auto invocation = run_cli(working, {"--config", pack().config().string()});
    ASSERT_EQ(0, invocation.status) << invocation.output;

    const auto folder = working / configured_folder();
    const auto written = files_under(folder);
    ASSERT_FALSE(written.empty()) << invocation.output;

    // Every file the run produced is named in what the program printed, so a user can find them
    // without guessing. `cli-output.txt` is this test's own capture and is not under the folder.
    for (const auto &relative : written) {
        EXPECT_NE(std::string::npos, invocation.output.find(relative.filename().string()))
            << relative << " was written and not reported:\n"
            << invocation.output;
    }
}

TEST_P(CliRun, TheManifestItWritesNamesTheConfigurationItRan) {
    const auto working = pack_scratch("cli_run_manifest");
    const auto invocation = run_cli(working, {"--config", pack().config().string()});
    ASSERT_EQ(0, invocation.status) << invocation.output;

    std::filesystem::path manifest;
    for (const auto &relative : files_under(working / configured_folder())) {
        if (relative.filename().string().ends_with("_manifest.json")) {
            manifest = working / configured_folder() / relative;
        }
    }
    ASSERT_FALSE(manifest.empty()) << invocation.output;

    std::ifstream stream{manifest};
    const auto document = nlohmann::json::parse(stream);
    EXPECT_EQ(pack().config().string(), document.at("config").at("path").get<std::string>());
    EXPECT_EQ(hgps::test::config_document(pack()).at("running").at("seed").get<std::uint32_t>(),
              document.at("seed").get<std::uint32_t>());
}

TEST_P(CliRun, ADryRunWritesNothingAtAll) {
    const auto working = pack_scratch("cli_dry_run");
    const auto invocation =
        run_cli(working, {"--config", pack().config().string(), "--dry-run"});
    ASSERT_EQ(0, invocation.status) << invocation.output;

    EXPECT_FALSE(std::filesystem::exists(working / configured_folder()))
        << "a dry run created its output folder";
}

TEST_P(CliRun, AConfigurationThatIsNotThereIsReportedAndNothingIsWritten) {
    const auto working = pack_scratch("cli_missing");
    const auto invocation = run_cli(working, {"--config", (working / "absent.json").string()});

    EXPECT_NE(0, invocation.status);
    EXPECT_NE(std::string::npos, invocation.output.find("absent.json")) << invocation.output;
    EXPECT_FALSE(std::filesystem::exists(working / configured_folder()));
}
