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

/// @brief Writes a **second** model pack over the same data, arranged so that nothing about the
///        first is safe to assume.
///
/// The first pack is tidy in every way a test could come to depend on: its model files sit beside
/// its config, its output is `result.csv` in `results/`, it has no active intervention and it
/// names all three of the data pack's diseases. A run of this project found a hard-coded output
/// file name that forty-five passing server tests had missed, because every one of them used that
/// pack (docs/SUMMARY.md). So this pack differs on each of those axes at once:
///
/// - model files and tables in **subdirectories**, named nothing like the first pack's;
/// - `output.folder` **nested three deep**, `output.file_name` carrying a `{TIMESTAMP}` token, so
///   the result's name is neither `result.csv` nor the same twice;
/// - an **active intervention**, so the run has two scenarios rather than one;
/// - a **different disease set**, in a different order, and one fewer comorbidity column;
/// - a different seed, horizon, cohort fraction and age range;
/// - and, since the eighth run, a **`StaticLinear` static model** with a categorical income, a
///   region, an ethnicity, a sector and a physical-activity value — which is what makes this the
///   pack that produces the income-stratified output files, the only output family no fixture
///   produced before (docs/decisions/0047-the-second-pack-carries-the-stratified-dimensions.md).
///
/// It refers to the same data pack as `../data`, so it costs no extra data generation.
///
/// @param output The directory to write into.
/// @return The number of files written.
std::size_t write_variant_model_pack(const std::filesystem::path &output,
                                     const FixturePackSpec &spec);

} // namespace hgps::tools
