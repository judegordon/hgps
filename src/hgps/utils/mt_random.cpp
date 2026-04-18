#include "mt_random.h"

namespace hgps {

MTRandom32::MTRandom32(unsigned int seed) { engine_.seed(seed); }

void MTRandom32::seed(unsigned int seed) { engine_.seed(seed); }

void MTRandom32::discard(unsigned long long skip) { engine_.discard(skip); }

unsigned int MTRandom32::next() { return engine_(); }

double MTRandom32::next_double() noexcept {
    return std::generate_canonical<double, std::numeric_limits<double>::digits>(engine_);
}

} // namespace hgps