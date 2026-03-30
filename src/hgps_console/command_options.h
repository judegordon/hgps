#pragma once

#include <cxxopts.hpp>

#include "hgps_input/data/data_source.h"

#include <optional>
#include <string>

namespace hgps {
struct CommandOptions {
    std::string config_source;

    std::optional<hgps::input::DataSource> data_source;

    std::optional<std::string> output_folder;

    bool verbose{};

    int job_id{};

    size_t num_threads{};

    bool dry_run{};
};

cxxopts::Options create_options();

std::optional<CommandOptions> parse_arguments(cxxopts::Options &options, int argc, char **argv);

} // namespace hgps
