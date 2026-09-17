// New here. Determinism contract clauses D3 and D5
// (docs/decisions/0026-parallelism-and-fixed-order-reductions.md). The baseline accumulates into
// a shared table under a mutex, which is race-free but order-variable (audit N-7); this checks
// that the replacement is bit-identical at any thread count.
#include "core/parallel.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

// Values chosen so that summing them in a different order gives a different double: a long tail
// of small numbers after a large one.
std::vector<double> catastrophic_values(std::size_t n) {
    std::vector<double> values;
    values.reserve(n);
    values.push_back(1.0e16);
    for (std::size_t i = 1; i < n; ++i) {
        values.push_back(1.0 + static_cast<double>(i % 7) * 0.1);
    }
    return values;
}

} // namespace

TEST(TestCore_Parallel, ForEachIndexVisitsEveryIndexExactlyOnce) {
    using namespace hgps::core::parallel;

    const WorkerCountScope workers{4};
    const std::size_t n = 10'000;
    std::vector<std::atomic<int>> visits(n);

    for_each_index(n, [&visits](std::size_t i) { visits[i].fetch_add(1); });

    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_EQ(1, visits[i].load()) << "index " << i;
    }

}

TEST(TestCore_Parallel, ForEachIndexOfZeroDoesNothing) {
    using namespace hgps::core::parallel;

    int calls = 0;
    for_each_index(0, [&calls](std::size_t) { ++calls; });
    EXPECT_EQ(0, calls);
}

TEST(TestCore_Parallel, InParallelRegionIsTrueOnlyInside) {
    using namespace hgps::core::parallel;

    EXPECT_FALSE(in_parallel_region());

    const WorkerCountScope workers{3};
    std::atomic<int> seen_inside{0};
    for_each_index(64, [&seen_inside](std::size_t) {
        if (in_parallel_region()) {
            seen_inside.fetch_add(1);
        }
    });

    EXPECT_EQ(64, seen_inside.load());
    EXPECT_FALSE(in_parallel_region());
}

TEST(TestCore_Parallel, ReduceOrderedIsIdenticalAtEveryThreadCount) {
    using namespace hgps::core::parallel;

    // More than one block, and deliberately not a multiple of the block size.
    const auto values = catastrophic_values(kBlockSize * 3 + 137);

    std::set<double> results;
    for (const std::size_t workers : {1U, 2U, 3U, 8U}) {
        const WorkerCountScope scope{workers};
        const double sum = reduce_ordered(
            values.size(), 0.0, [&values](std::size_t i) { return values[i]; },
            [](double a, double b) { return a + b; });
        results.insert(sum);
    }

    ASSERT_EQ(1U, results.size()) << "reduce_ordered gave different sums at different thread counts";

    // And the value is the one a fixed block decomposition produces, which is not the same as a
    // plain left-to-right accumulation of the whole range — that is the point of fixing the order
    // rather than hoping the order does not matter.
    const std::size_t block_count = (values.size() + kBlockSize - 1) / kBlockSize;
    double expected = 0.0;
    for (std::size_t block = 0; block < block_count; ++block) {
        double local = 0.0;
        const std::size_t begin = block * kBlockSize;
        const std::size_t end = std::min(begin + kBlockSize, values.size());
        for (std::size_t i = begin; i < end; ++i) {
            local += values[i];
        }
        expected += local;
    }
    EXPECT_DOUBLE_EQ(expected, *results.begin());
}

TEST(TestCore_Parallel, ReduceOrderedOfEmptyRangeIsTheIdentity) {
    using namespace hgps::core::parallel;

    const double sum = reduce_ordered(
        0, 0.0, [](std::size_t) { return 1.0; }, [](double a, double b) { return a + b; });
    EXPECT_DOUBLE_EQ(0.0, sum);
}

TEST(TestCore_Parallel, AnExceptionInABodyReachesTheCaller) {
    using namespace hgps::core::parallel;

    const WorkerCountScope workers{4};
    EXPECT_THROW(for_each_index(1'000,
                                [](std::size_t i) {
                                    if (i == 500) {
                                        throw std::runtime_error("body failed");
                                    }
                                }),
                 std::runtime_error);
}
