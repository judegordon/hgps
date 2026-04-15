#pragma once

#include "config.h"
#include "hgps_core/diagnostics.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

namespace hgps::input {

std::optional<FileInfo> get_file_info(const nlohmann::json &node,
                                      const std::filesystem::path &base_dir,
                                      hgps::core::InputIssueReport &diagnostics,
                                      std::string_view source_path = {},
                                      std::string_view field_path = {});

std::optional<SettingsInfo> get_settings(const nlohmann::json &j,
                                         hgps::core::InputIssueReport &diagnostics,
                                         std::string_view source_path = {},
                                         std::string_view field_path = {});

std::optional<BaselineInfo> get_baseline_info(const nlohmann::json &j,
                                              const std::filesystem::path &base_dir,
                                              hgps::core::InputIssueReport &diagnostics,
                                              std::string_view source_path = {},
                                              std::string_view field_path = {});

void load_interventions(const nlohmann::json &running,
                        Configuration &config,
                        hgps::core::InputIssueReport &diagnostics,
                        std::string_view source_path = {},
                        std::string_view field_path = {});

} // namespace hgps::input