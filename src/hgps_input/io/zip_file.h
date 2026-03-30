#pragma once

#include <filesystem>

namespace hgps::input {
std::filesystem::path get_zip_cache_directory(const std::string &file_hash);

void extract_zip_file(const std::filesystem::path &file_path,
                      const std::filesystem::path &output_directory);
} // namespace hgps::input
