#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace hgps::input {
void validate_json(std::istream &is, const std::string &schema_file_name, int schema_version);

nlohmann::json load_and_validate_json(const std::filesystem::path &file_path,
                                      const std::string &schema_file_name, int schema_version,
                                      bool require_schema_property = true);
} // namespace hgps::input
