// The documented example, run, and checked against the document.
#include "api_example.h"

#include "support/fixture_packs.h"
#include "support/simulation_harness.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// @brief The lines of `text` between the two markers, exclusive.
std::vector<std::string> marked_region(const std::string &text, const std::string &begin,
                                       const std::string &end) {
    std::istringstream stream{text};
    std::vector<std::string> lines;
    std::string line;
    bool inside = false;
    while (std::getline(stream, line)) {
        if (!inside) {
            inside = line.find(begin) != std::string::npos;
            continue;
        }
        if (line.find(end) != std::string::npos) {
            return lines;
        }
        lines.push_back(line);
    }
    return {};
}

/// @brief The contents of the first ```cpp fenced block in a Markdown document.
std::vector<std::string> first_cpp_block(const std::string &text) {
    std::istringstream stream{text};
    std::vector<std::string> lines;
    std::string line;
    bool inside = false;
    while (std::getline(stream, line)) {
        if (!inside) {
            inside = line == "```cpp";
            continue;
        }
        if (line == "```") {
            return lines;
        }
        lines.push_back(line);
    }
    return {};
}

} // namespace

TEST(ApiExample, TheDocumentQuotesThisFile) {
    // A usage example that is not compiled is a guess about the API. This one is compiled, run by
    // the test below, and identical to what the document shows — checked here, so the document
    // cannot drift away from the code it claims to quote.
    const auto source =
        read_file(std::filesystem::path{HGPS_SOURCE_DIR} / "tests/engine/api_example.cpp");
    const auto document = read_file(std::filesystem::path{HGPS_SOURCE_DIR} / "docs/api.md");

    const auto marked = marked_region(source, "docs/api.md example: begin",
                                      "docs/api.md example: end");
    const auto quoted = first_cpp_block(document);

    ASSERT_FALSE(marked.empty()) << "the markers in api_example.cpp were not found";
    ASSERT_FALSE(quoted.empty()) << "docs/api.md has no ```cpp block";
    EXPECT_EQ(marked, quoted) << "docs/api.md's example and tests/engine/api_example.cpp have "
                                 "drifted apart; the file is the original.";
}

namespace {

/// The published example runs against both synthetic packs, because it is the one piece of code in
/// the tree a reader is invited to copy (tests/support/fixture_packs.h).
class ApiExampleRun : public hgps::test::FixturePackTest {};

HGPS_TEST_EVERY_FIXTURE_PACK(ApiExampleRun);

} // namespace

TEST_P(ApiExampleRun, ItRunsAndWritesResults) {
    auto document = hgps::test::config_document(pack());
    const auto folder = pack_scratch("api_example_out");
    document["output"]["folder"] = folder.string();
    const auto config = hgps::test::write_config_variant(pack(), "api_example", document);

    EXPECT_EQ(0, hgps::example::run_one(config));

    // It says it writes what it lists, so something has to be there.
    std::size_t files = 0;
    for (const auto &entry : std::filesystem::directory_iterator{folder}) {
        files += entry.is_regular_file() ? 1U : 0U;
    }
    EXPECT_GE(files, 3U) << "expected at least the result CSV, its metadata and the manifest";
}

TEST(ApiExample, ItReportsABadConfigurationAndDoesNotThrow) {
    const auto missing = hgps::test::scratch_dir("api_example_missing") / "absent.json";
    EXPECT_EQ(3, hgps::example::run_one(missing));
}
