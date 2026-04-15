#pragma once

#include "config/config.h"
#include "hgps_core/diagnostics/input_issue.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hgps::input {

int get_model_schema_version(const std::string &model_name);

nlohmann::json load_json(const std::filesystem::path &filepath,
                         hgps::core::InputIssueReport &diagnostics,
                         std::string_view field_path = {});

std::optional<std::pair<std::string, nlohmann::json>>
load_and_validate_model_json(const std::filesystem::path &model_path,
                             hgps::core::InputIssueReport &diagnostics);

hgps::core::Income map_income_category(const std::string &key,
                                       int category_count,
                                       hgps::core::InputIssueReport &diagnostics,
                                       std::string_view source_path = {},
                                       std::string_view field_path = {});

} // namespace hgps::input