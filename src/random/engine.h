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
    ///
    /// std::mt19937::result_type is std::uint_fast32_t, which libc++ makes 32 bits wide and
    /// libstdc++ on LP64 makes 64. The engine's word size w is 32 either way, so every value it
    /// produces fits in std::uint32_t and the cast is value-preserving — the static_assert on
    /// max() below is what says so. Without the cast the narrowing is implicit, which is an
    /// error under -Wconversion on Linux and builds silently on macOS (docs/build-notes.md).
    result_type next() { return static_cast<result_type>(engine_()); }

    /// @brief Advances the state without producing values.
    void discard(unsigned long long skip) { engine_.discard(skip); }

    static constexpr result_type min() { return static_cast<result_type>(std::mt19937::min()); }
    static constexpr result_type max() { return static_cast<result_type>(std::mt19937::max()); }

  private:
    std::mt19937 engine_;
};

static_assert(MtEngine::min() == 0);
static_assert(MtEngine::max() == std::numeric_limits<std::uint32_t>::max(),
              "next_int's rejection sampling assumes the engine spans the whole 32-bit range.");
static_assert(std::mt19937::max() == std::numeric_limits<std::uint32_t>::max(),
              "next()'s narrowing cast is only value-preserving while the engine's range is "
              "exactly the 32-bit one, whatever width its result_type happens to be.");

} // namespace hgps::rng
