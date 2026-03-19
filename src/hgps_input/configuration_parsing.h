#pragma once
#include "configuration.h"

#include <optional>

namespace hgps::input {
void check_version(const nlohmann::json &j);

void load_input_info(const nlohmann::json &j, Configuration &config);

void load_modelling_info(const nlohmann::json &j, Configuration &config);

void load_running_info(const nlohmann::json &j, Configuration &config);

void load_output_info(const nlohmann::json &j, Configuration &config,
                      const std::optional<std::string> &output_folder);
} // namespace hgps::input
