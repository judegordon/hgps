#pragma once

#include <filesystem>

namespace hgps::example {

/// @brief The minimal usage example from docs/api.md, declared so a test can call it.
int run_one(const std::filesystem::path &config_path);

} // namespace hgps::example
