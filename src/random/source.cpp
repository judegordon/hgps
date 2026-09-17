#include "source.h"

#include "core/parallel.h"
#include "diagnostics/internal_error.h"

#include <cmath>
#include <limits>

#include <fmt/format.h>

namespace hgps::rng {

void RandomSource::reseed(std::uint32_t seed) {
    engine_ = MtEngine{seed};
    draw_count_ = 0;
}

std::uint32_t RandomSource::next_bits() {
    if (core::parallel::in_parallel_region()) {
        // Determinism contract D3. Reaching here means a callable passed to a parallel helper
        // captured an RNG handle: the draw order would then depend on thread scheduling, so the
        // run would not be reproducible. Failing here, on the first draw, with a source location,
        // is the whole point.
        throw diag::InternalError(
            "A random draw was attempted inside a parallel region. Random draws must happen on "
            "the sequential path only; see docs/design.md section 4, clause D3.");
    }

    ++draw_count_;
    return engine_.next();
}

double RandomSource::next_double() {
    // 53 bits, the width of a double's significand: 32 from one draw, 21 from the next.
    const std::uint64_t high = next_bits();
    const std::uint64_t low = next_bits();
    const std::uint64_t bits = (high << 32) | low;
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
}

double RandomSource::next_uniform(double lower, double upper) {
    if (!(upper >= lower)) {
        throw diag::InternalError(
            fmt::format("next_uniform needs lower <= upper, got [{}, {}).", lower, upper));
    }
    return lower + (upper - lower) * next_double();
}

std::size_t RandomSource::next_int(std::size_t count) {
    if (count == 0) {
        throw diag::InternalError("next_int(count) needs a count of at least 1; the range is "
                                  "half-open, so a count of 0 has no values to return.");
    }
    if (count == 1) {
        return 0;
    }

    // Rejection sampling. The 2^32 outputs of the engine are split into `count` buckets of equal
    // size; whatever is left over at the top is rejected and redrawn, so every value in
    // [0, count) is equally likely. Modulo alone would favour the low values (the earlier
    // rewrite's regression), and a float multiply has an out-of-range edge (the baseline's).
    constexpr std::uint64_t range = 1ULL << 32;

    if (count >= range) {
        // 64-bit count on a 32-bit engine: compose two draws. Reachable only for populations
        // above four billion, but the contract should not quietly break there.
        const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() -
                                    (std::numeric_limits<std::uint64_t>::max() % count) - 1;
        for (;;) {
            const std::uint64_t high = next_bits();
            const std::uint64_t low = next_bits();
            const std::uint64_t draw = (high << 32) | low;
            if (draw <= limit) {
                return static_cast<std::size_t>(draw % count);
            }
        }
    }

    const std::uint64_t buckets = range / count;
    const std::uint64_t limit = buckets * count; // first rejected value
    for (;;) {
        const std::uint64_t draw = next_bits();
        if (draw < limit) {
            return static_cast<std::size_t>(draw / buckets);
        }
    }
}

int RandomSource::next_int_inclusive(int lower, int upper) {
    if (upper < lower) {
        throw diag::InternalError(
            fmt::format("next_int_inclusive needs lower <= upper, got [{}, {}].", lower, upper));
    }

    const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(upper) -
                                                 static_cast<std::int64_t>(lower)) +
                      1ULL;
    const auto offset = next_int(static_cast<std::size_t>(span));
    return static_cast<int>(static_cast<std::int64_t>(lower) +
                            static_cast<std::int64_t>(offset));
}

double RandomSource::next_normal() { return next_normal(0.0, 1.0); }

double RandomSource::next_normal(double mean, double standard_deviation) {
    if (!(standard_deviation > 0.0)) {
        throw diag::InternalError(fmt::format(
            "next_normal needs a standard deviation greater than zero, got {}.",
            standard_deviation));
    }

    // Marsaglia polar method. p == 0.0 is rejected as well as p >= 1.0: the baseline rejected
    // only the upper end, so log(0) could propagate -inf and then NaN into a risk factor
    // (audit B-15). The probability is minute and the failure would be silent.
    double p = 0.0;
    double p1 = 0.0;
    double p2 = 0.0;
    do {
        p1 = next_uniform(-1.0, 1.0);
        p2 = next_uniform(-1.0, 1.0);
        p = p1 * p1 + p2 * p2;
    } while (p >= 1.0 || p == 0.0);

    return mean + standard_deviation * p1 * std::sqrt(-2.0 * std::log(p) / p);
}

namespace {

template <class CdfType>
void validate_empirical(std::span<const int> values, std::span<const CdfType> cdf) {
    if (values.empty() || cdf.empty()) {
        throw diag::InternalError("next_empirical_discrete needs a non-empty distribution.");
    }
    if (values.size() != cdf.size()) {
        throw diag::InternalError(fmt::format("next_empirical_discrete input size mismatch: {} "
                                              "values vs {} cumulative probabilities.",
                                              values.size(), cdf.size()));
    }
}

template <class CdfType>
int sample_empirical(std::span<const int> values, std::span<const CdfType> cdf, double draw) {
    for (std::size_t i = 0; i < cdf.size(); ++i) {
        if (draw <= static_cast<double>(cdf[i])) {
            return values[i];
        }
    }

    return values.back();
}

} // namespace

int RandomSource::next_empirical_discrete(std::span<const int> values,
                                          std::span<const float> cdf) {
    // Validate before drawing: a rejected call must not consume a draw, or a caught-and-reported
    // programmer error would shift every subsequent value in the stream.
    validate_empirical(values, cdf);
    return sample_empirical(values, cdf, next_double());
}

int RandomSource::next_empirical_discrete(std::span<const int> values,
                                          std::span<const double> cdf) {
    validate_empirical(values, cdf);
    return sample_empirical(values, cdf, next_double());
}

} // namespace hgps::rng
