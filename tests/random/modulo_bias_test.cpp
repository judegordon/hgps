// New here, required by the validation plan. The earlier rewrite replaced the baseline's float
// multiply with `% range`, which removed an out-of-range edge and introduced modulo bias
// (docs/audit/09-ideas-and-questions.md section 2). This test fails for either implementation.
#include "random/source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

TEST(TestRandom_ModuloBias, LargeRangeIsUniformAcrossBuckets) {
    // A range chosen to be a poor divisor of 2^32: with `% count`, the first
    // (2^32 mod count) values are each one draw more likely than the rest. Split the range into
    // two halves; modulo bias makes the low half measurably heavier.
    const std::size_t count = 3'000'000'000U;
    hgps::rng::RandomSource random{20260917U};

    const int n = 400'000;
    int low_half = 0;
    for (int i = 0; i < n; ++i) {
        const auto value = random.next_int(count);
        ASSERT_LT(value, count);
        if (value < count / 2) {
            ++low_half;
        }
    }

    const double share = static_cast<double>(low_half) / n;

    // With rejection sampling the expected share is 0.5. With `%` over this count it is about
    // 0.5 + 0.0776, which is nearly 50 standard errors away (one SE is about 0.0008 at n =
    // 400,000), so the bound below separates the two implementations decisively while leaving
    // ample room for sampling noise.
    EXPECT_NEAR(0.5, share, 0.005) << "integer sampling is biased towards the low half of the "
                                      "range, which is what modulo reduction does";
}

TEST(TestRandom_ModuloBias, SmallRangesAreUniformToWithinSamplingNoise) {
    hgps::rng::RandomSource random{424242U};

    for (const std::size_t count : {2U, 3U, 5U, 7U, 10U, 100U}) {
        std::vector<int> counts(count, 0);
        const int n = 200'000;
        for (int i = 0; i < n; ++i) {
            counts[random.next_int(count)] += 1;
        }

        const double expected = static_cast<double>(n) / static_cast<double>(count);
        double chi_squared = 0.0;
        for (const int observed : counts) {
            const double delta = static_cast<double>(observed) - expected;
            chi_squared += delta * delta / expected;
        }

        // Upper 0.1% point of chi-squared with (count - 1) degrees of freedom, rounded up:
        // generous, because this test must not be flaky, and still far below what a biased
        // sampler produces at this sample size.
        const double limit = 3.0 * static_cast<double>(count) + 30.0;
        EXPECT_LT(chi_squared, limit) << "count = " << count;
    }
}

TEST(TestRandom_ModuloBias, RejectionNeverReturnsAValueOutsideTheRange) {
    // The baseline's float-multiply scheme could in principle return max + 1 when next_double()
    // returned exactly 1.0 (audit B-07 with N-5).
    hgps::rng::RandomSource random{99U};

    for (const std::size_t count : {1U, 2U, 17U, 1'000U}) {
        for (int i = 0; i < 50'000; ++i) {
            ASSERT_LT(random.next_int(count), count) << "count = " << count;
        }
    }
}
