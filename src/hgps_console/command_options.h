#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <cxxopts.hpp>

#include "hgps_input/data/data_source.h"

namespace hgps {

struct CommandOptions {
    std::string config_source;
    std::optional<hgps::input::DataSource> data_source;
    std::optional<std::string> output_folder;
    bool verbose{false};
    int job_id{0};
    std::size_t num_threads{0};
    bool dry_run{false};
};

cxxopts::Options create_options();
std::optional<CommandOptions> parse_arguments(cxxopts::Options &options, int argc, char **argv);

} // namespace hgps