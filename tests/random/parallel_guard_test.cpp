// New here. Determinism contract clause D3: an RNG draw inside a parallel region must fail loudly
// instead of producing a number that depends on thread scheduling.
//
// The baseline holds this property by convention only — audit N-4 records that adding one
// context.random() call inside any existing tbb::parallel_for_each would introduce both a data
// race and nondeterminism, and would compile without complaint. The earlier rewrite kept the same
// conventional guarantee and missed a site (finding R-04).
#include "core/parallel.h"
#include "diagnostics/internal_error.h"
#include "random/source.h"

#include <gtest/gtest.h>

#include <vector>

TEST(TestRandom_ParallelGuard, DrawingInsideAParallelRegionThrows) {
    using namespace hgps::core::parallel;
    hgps::rng::RandomSource random{1234U};

    const WorkerCountScope workers{4};
    EXPECT_THROW(for_each_index(256, [&random](std::size_t) { random.next_double(); }),
                 hgps::diag::InternalError);
}

TEST(TestRandom_ParallelGuard, TheGuardAlsoAppliesWhenRunningSingleThreaded) {
    // The sequential path takes the same guard, so the mistake is caught in a one-thread run and
    // in a debug build, not only when the machine happens to have cores spare.
    using namespace hgps::core::parallel;
    hgps::rng::RandomSource random{1234U};

    const WorkerCountScope workers{1};
    EXPECT_THROW(for_each_index(4, [&random](std::size_t) { random.next_int(10); }),
                 hgps::diag::InternalError);
}

TEST(TestRandom_ParallelGuard, DrawingOutsideARegionIsFine) {
    using namespace hgps::core::parallel;
    hgps::rng::RandomSource random{1234U};

    const WorkerCountScope workers{4};
    // A region that does RNG-free work, then draws afterwards on the sequential path: the
    // supported pattern.
    std::vector<double> squares(128, 0.0);
    for_each_index(squares.size(), [&squares](std::size_t i) {
        squares[i] = static_cast<double>(i) * static_cast<double>(i);
    });

    EXPECT_NO_THROW(random.next_double());
    EXPECT_DOUBLE_EQ(127.0 * 127.0, squares.back());
}

TEST(TestRandom_ParallelGuard, TheThrownErrorCarriesASourceLocation) {
    using namespace hgps::core::parallel;
    hgps::rng::RandomSource random{1234U};

    const WorkerCountScope workers{2};
    try {
        for_each_index(8, [&random](std::size_t) { random.next_normal(); });
        FAIL() << "expected an InternalError";
    } catch (const hgps::diag::InternalError &error) {
        EXPECT_NE(nullptr, error.file_name());
        EXPECT_GT(error.line(), 0U);
        EXPECT_NE(std::string::npos, error.describe().find("parallel region"));
    }
}
