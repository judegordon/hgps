#pragma once

#include <filesystem>
#include <string>

namespace hgps::test {

// Paths come from the build (tests/CMakeLists.txt), not from __FILE__ arithmetic. The baseline
// derived its FINCH fixture path from __FILE__ and silently skipped 35 tests when the directory
// was absent (audit B-11); here a missing fixture is a failure with a path in the message.

inline std::filesystem::path fixtures_dir() { return std::filesystem::path{HGPS_TEST_DATA_DIR}; }

/// @brief The generated synthetic fixture pack. Generated at build time, so a test that cannot
///        find it should fail rather than skip (audit B-11).
inline std::filesystem::path synthetic_pack_dir() {
    return std::filesystem::path{HGPS_FIXTURE_PACK_DIR};
}

/// @brief The synthetic data store, in the upstream layout.
inline std::filesystem::path synthetic_data_dir() { return synthetic_pack_dir() / "data"; }

/// @brief The synthetic model pack: a runnable config v2 and its model definitions.
inline std::filesystem::path synthetic_model_dir() { return synthetic_pack_dir() / "model"; }

/// @brief The runnable synthetic config.
inline std::filesystem::path synthetic_config() { return synthetic_model_dir() / "config.json"; }

inline std::filesystem::path examples_dir() { return std::filesystem::path{HGPS_EXAMPLES_DIR}; }

inline std::filesystem::path upstream_examples_dir() {
    return std::filesystem::path{HGPS_UPSTREAM_EXAMPLES_DIR};
}

inline std::filesystem::path schemas_dir() { return std::filesystem::path{HGPS_SCHEMAS_DIR}; }

/// @brief The documentation tree, for the tests that check code and prose have not drifted apart.
inline std::filesystem::path docs_dir() {
    return std::filesystem::path{HGPS_SOURCE_DIR} / "docs";
}

/// @brief A scratch directory for a test that must write files, removed and recreated on request.
std::filesystem::path scratch_dir(const std::string &test_name);

} // namespace hgps::test
