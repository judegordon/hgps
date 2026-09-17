#pragma once

#include <cstdint>
#include <limits>
#include <random>

namespace hgps::rng {

/// @brief The Mersenne Twister engine, seeded explicitly or not at all.
///
/// There is no default constructor and no seed() setter. The baseline's default constructor
/// seeded from std::random_device, so a config without `running.seed` ran irreproducibly and
/// silently, and the results file then recorded the seed as 0 (audit B-06). Requiring a seed at
/// construction turns that runtime hazard into a compile error: determinism contract clause D1.
///
/// std::random_device appears nowhere in this project, and a test greps for it.
class MtEngine final {
  public:
    using result_type = std::uint32_t;

    MtEngine() = delete;

    explicit MtEngine(std::uint32_t seed) : engine_{seed} {}

    /// @brief The next raw 32-bit draw.
    result_type next() { return engine_(); }

    /// @brief Advances the state without producing values.
    void discard(unsigned long long skip) { engine_.discard(skip); }

    static constexpr result_type min() { return std::mt19937::min(); }
    static constexpr result_type max() { return std::mt19937::max(); }

  private:
    std::mt19937 engine_;
};

static_assert(MtEngine::min() == 0);
static_assert(MtEngine::max() == std::numeric_limits<std::uint32_t>::max(),
              "next_int's rejection sampling assumes the engine spans the whole 32-bit range.");

} // namespace hgps::rng
