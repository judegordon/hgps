#pragma once

#include <filesystem>
#include <string>

namespace hgps::input {
void download_file(const std::string &url, const std::filesystem::path &download_path);

std::filesystem::path download_file_to_temporary(const std::string &url,
                                                 const std::string &file_extension);
} // namespace hgps::input
