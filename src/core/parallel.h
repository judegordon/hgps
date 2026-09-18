#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace hgps::core::parallel {

// The only code in this project that creates a thread.
//
// Two rules from the determinism contract (docs/design.md section 4) shape this interface:
//
//  D3  A callable passed to for_each_index or reduce_ordered receives an index and nothing else.
//      There is no parameter through which it could be handed an RNG handle. While a region is
//      running, in_parallel_region() is true on every worker AND on the calling thread, and every
//      rng::RandomSource draw checks it — so a lambda that captures a RandomSource by reference
//      fails on its first draw with a source location instead of quietly producing a different
//      number on the next run.
//
//  D5  reduce_ordered's block decomposition is a function of n and kBlockSize only, never of the
//      thread count, and partial results are combined in ascending block order. The result is
//      therefore bit-identical at any thread count, which is what the reproducibility test checks.

/// @brief Sets the worker count for subsequent regions. 1 means "run inline, create no thread".
///
/// The application sets this once at start-up from --threads. Changing it from inside a running
/// region is a programmer error and throws: the block decomposition of a reduction must not
/// change while the reduction is in flight.
///
/// It is deliberately settable more than once, because the reproducibility test runs the same
/// simulation at one thread and at N in a single process and compares the output byte for byte.
/// Prefer WorkerCountScope, which puts the previous value back.
void set_worker_count(std::size_t workers);

/// @brief Sets the worker count for a scope and restores the previous value on the way out.
class WorkerCountScope final {
  public:
    explicit WorkerCountScope(std::size_t workers);
    ~WorkerCountScope();

    WorkerCountScope(const WorkerCountScope &) = delete;
    WorkerCountScope &operator=(const WorkerCountScope &) = delete;
    WorkerCountScope(WorkerCountScope &&) = delete;
    WorkerCountScope &operator=(WorkerCountScope &&) = delete;

  private:
    std::size_t previous_;
};

std::size_t worker_count() noexcept;

/// @brief True while this thread is inside a parallel region.
bool in_parallel_region() noexcept;

/// @brief Applies `body` to every index in [0, n), possibly out of order and concurrently.
///
/// `body` must be a pure function of the state it reads: no RNG, no mutation of anything another
/// index also touches, no dependence on visit order.
void for_each_index(std::size_t n, const std::function<void(std::size_t)> &body);

/// @brief The block size used by reduce_ordered.
///
/// 4096 keeps the number of partials small for a national population (a few dozen blocks at
/// 125k people) while being large enough that per-block overhead is noise. It is a fixed
/// constant rather than a function of the thread count precisely so that the reduction order
/// does not change with the hardware.
inline constexpr std::size_t kBlockSize = 4096;

/// @brief A deterministic map-reduce over [0, n).
///
/// Each block accumulates in ascending index order, then the per-block partials are combined in
/// ascending block order. `map` must be a pure function of the index; `combine` must be
/// associative in intent, though it need not be exactly associative in floating point — the
/// fixed order is what makes the result reproducible regardless.
template <class T, class MapFn, class CombineFn>
T reduce_ordered(std::size_t n, T identity, MapFn map, CombineFn combine) {
    if (n == 0) {
        return identity;
    }

    const std::size_t block_count = (n + kBlockSize - 1) / kBlockSize;
    std::vector<T> partials(block_count, identity);

    for_each_index(block_count, [&](std::size_t block) {
        const std::size_t begin = block * kBlockSize;
        const std::size_t end = begin + kBlockSize < n ? begin + kBlockSize : n;

        T local = identity;
        for (std::size_t i = begin; i < end; ++i) {
            local = combine(std::move(local), map(i));
        }
        partials[block] = std::move(local);
    });

    T result = identity;
    for (std::size_t block = 0; block < block_count; ++block) {
        result = combine(std::move(result), std::move(partials[block]));
    }

    return result;
}

} // namespace hgps::core::parallel
