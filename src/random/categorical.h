#pragma once

#include "source.h"

#include "diagnostics/internal_error.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace hgps::rng {

/// @brief A discrete distribution over values of T, sampled in a stated order.
///
/// Constructible only from an ordered sequence. The baseline built a probability CDF by iterating
/// an std::unordered_map, so the same seed could assign a different income category on a
/// different standard library — the one place in that codebase where a seed does not determine
/// the model result across platforms (audit B-05 / N-11).
///
/// The unordered constructors below are declared and deleted so that mistake names itself at the
/// call site rather than becoming a portability bug nobody can reproduce. Determinism contract
/// clause D4; docs/decisions/0016-categorical-ordered-sampling.md.
template <class T> class Categorical final {
  public:
    /// @brief From parallel value and weight sequences, in the order given.
    /// @throws diag::InternalError for empty or mismatched inputs, a negative or non-finite
    ///         weight, or a total weight of zero.
    static Categorical from_weights(std::span<const T> values, std::span<const double> weights) {
        if (values.empty()) {
            throw diag::InternalError("A categorical distribution needs at least one value.");
        }
        if (values.size() != weights.size()) {
            throw diag::InternalError(
                fmt::format("Categorical input size mismatch: {} values vs {} weights.",
                            values.size(), weights.size()));
        }

        double total = 0.0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (!std::isfinite(weights[i]) || weights[i] < 0.0) {
                throw diag::InternalError(fmt::format(
                    "Categorical weight {} at index {} is not a finite, non-negative number.",
                    weights[i], i));
            }
            total += weights[i];
        }

        if (!(total > 0.0)) {
            throw diag::InternalError("Categorical weights sum to zero; nothing could be sampled.");
        }

        Categorical result;
        result.values_.assign(values.begin(), values.end());
        result.cdf_.reserve(weights.size());

        // The CDF is accumulated once, in the given order, and stored. Sampling then reads it
        // rather than re-summing, so repeated draws cannot disagree about the boundaries.
        double cumulative = 0.0;
        for (const double weight : weights) {
            cumulative += weight / total;
            result.cdf_.push_back(cumulative);
        }
        result.cdf_.back() = 1.0;

        return result;
    }

    /// @brief From (value, weight) pairs, in the order given.
    static Categorical from_pairs(std::span<const std::pair<T, double>> pairs) {
        std::vector<T> values;
        std::vector<double> weights;
        values.reserve(pairs.size());
        weights.reserve(pairs.size());
        for (const auto &[value, weight] : pairs) {
            values.push_back(value);
            weights.push_back(weight);
        }
        return from_weights(values, weights);
    }

    /// @brief From an ordered map, whose iteration order is its key order and therefore stated.
    static Categorical from_ordered_map(const std::map<T, double> &weights) {
        std::vector<T> values;
        std::vector<double> w;
        values.reserve(weights.size());
        w.reserve(weights.size());
        for (const auto &[value, weight] : weights) {
            values.push_back(value);
            w.push_back(weight);
        }
        return from_weights(values, w);
    }

    // Deliberately deleted: see the class comment. An unordered container has no sampling order.
    Categorical(const std::unordered_map<T, double> &) = delete;
    static Categorical from_weights(const std::unordered_map<T, double> &) = delete;

    std::size_t size() const noexcept { return values_.size(); }

    /// @brief The values, in sampling order.
    std::span<const T> values() const noexcept { return values_; }

    /// @brief The cumulative probabilities, in sampling order. The last is exactly 1.0.
    std::span<const double> cdf() const noexcept { return cdf_; }

    /// @brief The probability of the value at an index.
    double probability(std::size_t index) const {
        if (index >= cdf_.size()) {
            throw diag::InternalError(
                fmt::format("Categorical index {} is outside [0, {}).", index, cdf_.size()));
        }
        return index == 0 ? cdf_[0] : cdf_[index] - cdf_[index - 1];
    }

    /// @brief One draw. next_double() is in [0, 1), and the last CDF entry is exactly 1.0, so
    ///        the search always lands on a value.
    const T &sample(RandomSource &random) const {
        const double draw = random.next_double();
        const auto it = std::upper_bound(cdf_.begin(), cdf_.end(), draw);
        const auto index = static_cast<std::size_t>(std::distance(cdf_.begin(), it));
        return values_[index < values_.size() ? index : values_.size() - 1];
    }

  private:
    // Private, so the only routes in are the factories above, every one of which takes an
    // ordered sequence.
    Categorical() = default;

    std::vector<T> values_{};
    std::vector<double> cdf_{};
};

} // namespace hgps::rng
