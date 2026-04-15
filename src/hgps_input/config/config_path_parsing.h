#pragma once

#include "hgps_core/diagnostics.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string_view>

namespace hgps::input {

void rebase_valid_path(std::filesystem::path &path,
                       const std::filesystem::path &base_dir);

bool rebase_valid_path_to(const nlohmann::json &j,
                          const std::string &key,
                          std::filesystem::path &out,
                          const std::filesystem::path &base_dir,
                          hgps::core::InputIssueReport &diagnostics,
                          std::string_view source_path = {},
                          std::string_view field_path = {});

void rebase_valid_path_to(const nlohmann::json &j,
                          const std::string &key,
                          std::filesystem::path &out,
                          const std::filesystem::path &base_dir,
                          hgps::core::InputIssueReport &diagnostics,
                          bool &success,
                          std::string_view source_path = {},
                          std::string_view field_path = {});

} // namespace hgps::input