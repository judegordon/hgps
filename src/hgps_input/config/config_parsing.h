#pragma once

#include "config.h"
#include "hgps_core/diagnostics.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>

namespace hgps::input {

void check_version(const nlohmann::json &j,
                   hgps::core::InputIssueReport &diagnostics,
                   std::string_view source_path = {},
                   std::string_view field_path = {});

void load_input_info(const nlohmann::json &j,
                     Configuration &config,
                     hgps::core::InputIssueReport &diagnostics,
                     std::string_view source_path = {},
                     std::string_view field_path = {});

void load_modelling_info(const nlohmann::json &j,
                         Configuration &config,
                         hgps::core::InputIssueReport &diagnostics,
                         std::string_view source_path = {},
                         std::string_view field_path = {});

void load_running_info(const nlohmann::json &j,
                       Configuration &config,
                       hgps::core::InputIssueReport &diagnostics,
                       std::string_view source_path = {},
                       std::string_view field_path = {});

void load_output_info(const nlohmann::json &j,
                      Configuration &config,
                      const std::optional<std::string> &output_folder,
                      hgps::core::InputIssueReport &diagnostics,
                      std::string_view source_path = {},
                      std::string_view field_path = {});

} // namespace hgps::input