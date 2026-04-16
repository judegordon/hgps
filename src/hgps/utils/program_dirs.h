#pragma once

#include <filesystem>

namespace hgps {
std::filesystem::path get_program_directory();

std::filesystem::path get_program_path();

std::filesystem::path get_cache_directory();

std::filesystem::path get_temporary_directory();
} // namespace hgps
