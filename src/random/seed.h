#pragma once

#include <cstdint>

namespace hgps::rng {

/// @brief The seed for trial run `run_index` (0-based) of a job with this master seed.
///
/// A pure function of (master_seed, run_index), so adding a trial run does not change the seeds
/// of the runs before it. The baseline instead draws each run seed sequentially from a master
/// engine, which couples them: run 3's seed depends on how many runs came first.
///
/// The mixer is SplitMix64's finalising avalanche applied to (master << 32) | index, truncated to
/// 32 bits — small, well tested as a bit mixer, and specified here rather than delegated to a
/// library so the value cannot change under us.
std::uint32_t derive_run_seed(std::uint32_t master_seed, std::uint32_t run_index) noexcept;

/// @brief The seed for an HPC array job element, derived from the user's seed and the job id.
///
/// Same reasoning as derive_run_seed. The baseline derives this by seeding a throwaway engine and
/// discarding 1.618 * job_id * 2^16 draws, which costs time proportional to the job id.
std::uint32_t derive_job_seed(std::uint32_t master_seed, std::uint32_t job_id) noexcept;

} // namespace hgps::rng
