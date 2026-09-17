#include "test_paths.h"

namespace hgps::test {

std::filesystem::path scratch_dir(const std::string &test_name) {
    const auto path = std::filesystem::path{HGPS_BINARY_DIR} / "test-scratch" / test_name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

} // namespace hgps::test
