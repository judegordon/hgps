#pragma once

#include "randombit_generator.h"
#include <random>

namespace hgps {

class MTRandom32 final : public RandomBitGenerator {
  public:
    MTRandom32() = delete;

    explicit MTRandom32(unsigned int seed);

    void seed(unsigned int seed) override;

    void discard(unsigned long long skip) override;

    unsigned int next() override;

    double next_double() noexcept override;

    static constexpr unsigned int min() { return std::mt19937::min(); }

    static constexpr unsigned int max() { return std::mt19937::max(); }

  private:
    std::mt19937 engine_;
};

} // namespace hgps