#pragma once

#include "pack.h"

#include <filesystem>

namespace hgps::tools {

/// @brief Writes a runnable synthetic model pack: a config v2 file, a static and a dynamic model
///        definition, the FactorsMean tables and an input dataset.
///
/// Together with the data pack this makes a complete simulation that runs offline, which is what
/// the reproducibility test and the smoke test need. Every number is invented; see SYNTHETIC.md.
///
/// @param output The directory to write into. The config refers to the data pack as `../data`.
/// @return The number of files written.
std::size_t write_model_pack(const std::filesystem::path &output, const FixturePackSpec &spec);

} // namespace hgps::tools
