#pragma once

#include <limits>
#include <vector>

namespace hgps {

class RandomBitGenerator {
  public:
    using result_type = unsigned int;

    virtual ~RandomBitGenerator() = default;

    virtual void seed(const unsigned int seed) = 0;

    virtual void discard(const unsigned long long skip) = 0;

    virtual unsigned int next() = 0;

    virtual double next_double() noexcept = 0;

    static constexpr result_type min() { return std::numeric_limits<result_type>::min(); }

    static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
};
} // namespace hgps
