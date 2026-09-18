// The library/CLI boundary, checked rather than asserted in a comment.
//
// The CLI is meant to be a client of the published API: it may include include/hgps/ and its own
// two headers, and nothing else. CMake already enforces half of this — src/ is not on the include
// path of anything that links hgps::engine — but a relative include or a future change to the
// target's include directories could put it back, and the failure mode is silent: the program goes
// on working while the boundary quietly stops existing.
//
// So this reads the CLI's sources and checks what they include. Grepping source text as a test is
// unusual; it is the same tactic as the <cctype> check (determinism clause D12), and it is here for
// the same reason — the rule is about the shape of the tree, so the tree is what gets read.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

std::filesystem::path source_root() { return std::filesystem::path{HGPS_SOURCE_DIR}; }

std::vector<std::string> includes_of(const std::filesystem::path &path) {
    std::ifstream stream{path};
    const std::regex pattern{R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])"};

    std::vector<std::string> found;
    std::string line;
    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, pattern)) {
            found.push_back(match[1].str());
        }
    }
    return found;
}

/// @brief The standard library and the two third-party headers the project already depends on.
bool is_external(const std::string &header) {
    if (header.starts_with("fmt/") || header.starts_with("nlohmann/")) {
        return true;
    }
    // A standard header has no slash and, but for <cstdint> and friends, no extension either.
    return header.find('/') == std::string::npos && !header.ends_with(".h");
}

} // namespace

class CliBoundary : public ::testing::Test {
  protected:
    static std::vector<std::filesystem::path> cli_sources() {
        const auto app = source_root() / "src" / "app";
        std::vector<std::filesystem::path> sources;
        for (const auto &entry : std::filesystem::directory_iterator{app}) {
            const auto extension = entry.path().extension();
            if (extension == ".cpp" || extension == ".h") {
                sources.push_back(entry.path());
            }
        }
        std::sort(sources.begin(), sources.end());
        return sources;
    }
};

TEST_F(CliBoundary, TheCliHasSourcesToCheck) {
    // If this fails the rest of the suite is checking nothing, which is the failure mode a
    // source-reading test has to rule out first.
    const auto sources = cli_sources();
    ASSERT_FALSE(sources.empty()) << "no CLI sources under " << (source_root() / "src" / "app");
    EXPECT_NE(sources.end(), std::find_if(sources.begin(), sources.end(),
                                          [](const std::filesystem::path &path) {
                                              return path.filename() == "main.cpp";
                                          }));
}

TEST_F(CliBoundary, TheCliIncludesNothingOutsideThePublicApiAndItsOwnHeaders) {
    const std::set<std::string> own{"options.h", "reporter.h"};

    for (const auto &source : cli_sources()) {
        for (const auto &header : includes_of(source)) {
            if (is_external(header) || own.contains(header)) {
                continue;
            }
            EXPECT_TRUE(header.starts_with("hgps/"))
                << source.filename().string() << " includes \"" << header
                << "\", which is neither the public API (hgps/…) nor one of the CLI's own headers. "
                   "The CLI is a client of hgps::engine; see "
                   "docs/decisions/0032-library-and-a-thin-cli.md.";
        }
    }
}

TEST_F(CliBoundary, ThePublicHeadersIncludeNoInternalHeader) {
    // The other half of the same rule: a public header that pulls in src/ would make every caller
    // depend on the internals whether they wanted to or not.
    const auto include = source_root() / "include" / "hgps";
    ASSERT_TRUE(std::filesystem::is_directory(include)) << include;

    std::size_t checked = 0;
    for (const auto &entry : std::filesystem::directory_iterator{include}) {
        if (entry.path().extension() != ".h") {
            continue;
        }
        ++checked;
        for (const auto &header : includes_of(entry.path())) {
            if (is_external(header)) {
                continue;
            }
            const bool sibling = std::filesystem::is_regular_file(include / header);
            const bool qualified = header.starts_with("hgps/");
            EXPECT_TRUE(sibling || qualified)
                << entry.path().filename().string() << " includes \"" << header
                << "\", which is not a public header. include/hgps/ is the whole API surface.";
        }
    }
    EXPECT_GE(checked, 5U) << "expected at least the five published headers";
}
