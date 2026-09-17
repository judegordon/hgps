#pragma once

#include <filesystem>
#include <string>

namespace hgps::test {

// Paths come from the build (tests/CMakeLists.txt), not from __FILE__ arithmetic. The baseline
// derived its FINCH fixture path from __FILE__ and silently skipped 35 tests when the directory
// was absent (audit B-11); here a missing fixture is a failure with a path in the message.

inline std::filesystem::path fixtures_dir() { return std::filesystem::path{HGPS_TEST_DATA_DIR}; }

inline std::filesystem::path synthetic_pack_dir() { return fixtures_dir() / "pack"; }

inline std::filesystem::path examples_dir() { return std::filesystem::path{HGPS_EXAMPLES_DIR}; }

inline std::filesystem::path upstream_examples_dir() {
    return std::filesystem::path{HGPS_UPSTREAM_EXAMPLES_DIR};
}

inline std::filesystem::path schemas_dir() { return std::filesystem::path{HGPS_SCHEMAS_DIR}; }

/// @brief A scratch directory for a test that must write files, removed and recreated on request.
std::filesystem::path scratch_dir(const std::string &test_name);

} // namespace hgps::test
