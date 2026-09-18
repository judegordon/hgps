// Part of the public API of hgps::engine. Nothing here includes an internal header.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
#pragma once

#include <atomic>
#include <memory>

namespace hgps::api {

/// @brief Cooperative cancellation for a run in progress.
///
/// A copyable handle over one shared flag. `cancel()` may be called from any thread — a GUI's UI
/// thread, a signal handler's worker, a test — while `execute()` runs on another.
///
/// The engine checks the flag at the two points where stopping is safe and cheap: between
/// simulated years and between scenarios. It never checks it inside a population sweep, so
/// cancellation cannot land between two people and leave a half-updated cohort, and it never
/// consumes a random draw — so a cancelled run is a prefix of the run that would have happened,
/// and an uncancelled run is bit-for-bit what it would have been without a token
/// (docs/design.md section 4).
///
/// A default-constructed token is never cancelled and is what a caller that does not want
/// cancellation passes.
class CancellationToken {
  public:
    CancellationToken() : flag_{std::make_shared<std::atomic_bool>(false)} {}

    /// @brief Asks the run to stop at its next safe point. Idempotent; never blocks.
    void cancel() const noexcept { flag_->store(true, std::memory_order_relaxed); }

    bool cancelled() const noexcept { return flag_->load(std::memory_order_relaxed); }

  private:
    std::shared_ptr<std::atomic_bool> flag_;
};

} // namespace hgps::api
