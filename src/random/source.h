#pragma once

#include "engine.h"

#include <cstdint>
#include <span>
#include <vector>

namespace hgps::rng {

/// @brief The only way to draw a random number in this program.
///
/// Neither copyable nor movable and with no default constructor, so it cannot be duplicated into
/// a worker thread or stored in a container: determinism contract clause D2. It is owned by
/// RuntimeContext and handed out by reference.
///
/// Every draw checks core::parallel::in_parallel_region() and throws diag::InternalError if it is
/// set (clause D3). The parallel helpers pass their callables nothing but an index, so the only
/// way to reach an RNG from inside a region is to capture this object — and that fails loudly on
/// the first draw instead of producing a number that depends on thread scheduling.
///
/// The distributions are hand-written rather than taken from <random>, which is the baseline's
/// choice and the right one: std::normal_distribution and std::uniform_int_distribution are not
/// specified to produce the same sequence across standard libraries.
class RandomSource final {
  public:
    RandomSource() = delete;
    explicit RandomSource(std::uint32_t seed) : engine_{seed} {}

    RandomSource(const RandomSource &) = delete;
    RandomSource &operator=(const RandomSource &) = delete;
    RandomSource(RandomSource &&) = delete;
    RandomSource &operator=(RandomSource &&) = delete;

    /// @brief Restarts the stream from a new explicit seed, for the next trial run.
    /// @note There is deliberately no overload that picks a seed itself.
    void reseed(std::uint32_t seed);

    /// @brief A uniform double in [0, 1).
    ///
    /// Built from 53 random bits explicitly: (bits >> 11) * 2^-53. The upper bound is unreachable
    /// by construction, so nothing downstream has to defend against an exact 1.0. The baseline
    /// used std::generate_canonical, which is specified but has a history of returning 1.0 on some
    /// implementations (LWG 2524, audit N-5) — and its integer sampler would then return
    /// max + 1.
    double next_double();

    /// @brief A uniform double in [lower, upper).
    double next_uniform(double lower, double upper);

    /// @brief A uniform integer in [0, count) — half-open, as the name of the parameter says.
    ///
    /// Sampled by rejection from the engine's raw output: no modulo bias (the earlier rewrite's
    /// regression) and no out-of-range edge (the baseline's, B-07).
    /// @throws diag::InternalError if count is 0.
    std::size_t next_int(std::size_t count);

    /// @brief A uniform integer in [lower, upper], inclusive — the contract is in the name.
    /// @throws diag::InternalError if upper < lower.
    int next_int_inclusive(int lower, int upper);

    /// @brief A standard normal draw.
    double next_normal();

    /// @brief A normal draw.
    /// @throws diag::InternalError if standard_deviation <= 0.
    double next_normal(double mean, double standard_deviation);

    /// @brief Samples `values` against a cumulative distribution, returning the first value whose
    ///        cumulative probability reaches the draw.
    /// @throws diag::InternalError if the inputs are empty or of different sizes. The baseline
    ///         checked only the sizes, so two empty vectors read values.back() (B-14).
    int next_empirical_discrete(std::span<const int> values, std::span<const float> cdf);
    int next_empirical_discrete(std::span<const int> values, std::span<const double> cdf);

    /// @brief The number of draws taken from this stream. For diagnostics and tests only.
    std::uint64_t draw_count() const noexcept { return draw_count_; }

  private:
    MtEngine engine_;
    std::uint64_t draw_count_{0};

    std::uint32_t next_bits();
};

} // namespace hgps::rng
