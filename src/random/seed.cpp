#include "seed.h"

namespace hgps::rng {
namespace {

// SplitMix64's finaliser.
std::uint64_t mix64(std::uint64_t z) noexcept {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

std::uint32_t derive(std::uint32_t master_seed, std::uint32_t index, std::uint64_t domain) noexcept {
    const std::uint64_t input = (static_cast<std::uint64_t>(master_seed) << 32) | index;
    return static_cast<std::uint32_t>(mix64(input ^ domain) >> 32);
}

} // namespace

std::uint32_t derive_run_seed(std::uint32_t master_seed, std::uint32_t run_index) noexcept {
    // Distinct domain constants keep run seeds and job seeds from ever colliding for the same
    // (master, index) pair.
    return derive(master_seed, run_index, 0x5250554E52554E53ULL);
}

std::uint32_t derive_job_seed(std::uint32_t master_seed, std::uint32_t job_id) noexcept {
    return derive(master_seed, job_id, 0x4A4F42534A4F4253ULL);
}

} // namespace hgps::rng
