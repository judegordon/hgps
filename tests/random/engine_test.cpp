// New here, in part. The baseline has no test for its RNG wrapper at all, which is how B-06,
// B-07, B-14 and B-15 all survived. Each test below names the finding it pins.
#include "core/parallel.h"
#include "diagnostics/internal_error.h"
#include "random/engine.h"
#include "random/seed.h"
#include "random/source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <set>
#include <type_traits>
#include <vector>

using hgps::rng::MtEngine;
using hgps::rng::RandomSource;

// Determinism contract D1 and D2, checked at compile time: an unseeded or shared RNG must not be
// expressible. The baseline's default constructor seeded from std::random_device (B-06).
static_assert(!std::is_default_constructible_v<MtEngine>);
static_assert(!std::is_default_constructible_v<RandomSource>);
static_assert(!std::is_copy_constructible_v<RandomSource>);
static_assert(!std::is_copy_assignable_v<RandomSource>);
static_assert(!std::is_move_constructible_v<RandomSource>);
static_assert(!std::is_move_assignable_v<RandomSource>);

TEST(TestRandom_Engine, KnownMersenneTwisterSequence) {
    // The reference value for std::mt19937 seeded with 5489 (its default) after 10,000 draws,
    // from the standard: 4123659995. If this fails, the engine is not MT19937.
    MtEngine engine{5489U};
    std::uint32_t value = 0;
    for (int i = 0; i < 10'000; ++i) {
        value = engine.next();
    }
    EXPECT_EQ(4123659995U, value);
}

TEST(TestRandom_Engine, SameSeedSameSequence) {
    MtEngine a{12345U};
    MtEngine b{12345U};
    MtEngine c{12346U};

    bool differed = false;
    for (int i = 0; i < 100; ++i) {
        const auto left = a.next();
        EXPECT_EQ(left, b.next());
        differed = differed || (left != c.next());
    }
    EXPECT_TRUE(differed);
}

TEST(TestRandom_Engine, DiscardSkipsTheSameNumberOfDraws) {
    MtEngine a{99U};
    MtEngine b{99U};

    for (int i = 0; i < 17; ++i) {
        a.next();
    }
    b.discard(17);

    EXPECT_EQ(a.next(), b.next());
}

TEST(TestRandom_Source, NextDoubleIsInTheUnitIntervalAndNeverOne) {
    RandomSource random{2026U};

    double max_seen = 0.0;
    for (int i = 0; i < 200'000; ++i) {
        const double value = random.next_double();
        ASSERT_GE(value, 0.0);
        ASSERT_LT(value, 1.0);
        max_seen = std::max(max_seen, value);
    }

    // The 53-bit construction cannot reach 1.0, so nothing downstream needs to defend against it
    // (audit N-5: std::generate_canonical has returned exactly 1.0 on some implementations).
    EXPECT_GT(max_seen, 0.999);
}

TEST(TestRandom_Source, NextDoubleConsumesTwoDrawsPerValue) {
    RandomSource random{7U};
    EXPECT_EQ(0U, random.draw_count());
    random.next_double();
    EXPECT_EQ(2U, random.draw_count());
}

TEST(TestRandom_Source, ReseedRestartsTheStream) {
    RandomSource random{42U};
    const auto first = random.next_double();
    random.next_double();

    random.reseed(42U);
    EXPECT_EQ(0U, random.draw_count());
    EXPECT_DOUBLE_EQ(first, random.next_double());
}

TEST(TestRandom_Source, NextIntIsHalfOpen) {
    // Baseline finding B-07: next_int was documented as [min, max) and implemented inclusive of
    // max, and all four call sites compensated by passing size() - 1. Here the count form is
    // half-open and the inclusive form is a different name.
    RandomSource random{5U};

    std::set<std::size_t> seen;
    for (int i = 0; i < 10'000; ++i) {
        const auto value = random.next_int(4);
        ASSERT_LT(value, 4U);
        seen.insert(value);
    }
    EXPECT_EQ(4U, seen.size());

    EXPECT_EQ(0U, random.next_int(1));
    EXPECT_THROW(random.next_int(0), hgps::diag::InternalError);
}

TEST(TestRandom_Source, NextIntInclusiveCoversBothEnds) {
    RandomSource random{6U};

    std::set<int> seen;
    for (int i = 0; i < 10'000; ++i) {
        const int value = random.next_int_inclusive(-2, 2);
        ASSERT_GE(value, -2);
        ASSERT_LE(value, 2);
        seen.insert(value);
    }
    EXPECT_EQ(5U, seen.size());

    EXPECT_EQ(3, random.next_int_inclusive(3, 3));
    EXPECT_THROW(random.next_int_inclusive(5, 4), hgps::diag::InternalError);
}

TEST(TestRandom_Source, NormalDrawsHaveTheRequestedMomentsAndRejectBadParameters) {
    RandomSource random{11U};

    const int n = 200'000;
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double value = random.next_normal(2.0, 3.0);
        ASSERT_TRUE(std::isfinite(value)) << "draw " << i << " was not finite";
        sum += value;
        sum_sq += value * value;
    }

    const double mean = sum / n;
    const double variance = sum_sq / n - mean * mean;
    EXPECT_NEAR(2.0, mean, 0.05);
    EXPECT_NEAR(9.0, variance, 0.2);

    EXPECT_THROW(random.next_normal(0.0, 0.0), hgps::diag::InternalError);
    EXPECT_THROW(random.next_normal(0.0, -1.0), hgps::diag::InternalError);
}

TEST(TestRandom_Source, NormalDrawsAreFiniteOverManySamples) {
    // Baseline finding B-15: the polar method rejected p >= 1.0 but not p == 0.0, so log(0)
    // could propagate -inf and then NaN into a risk factor. p == 0.0 needs both uniforms to be
    // exactly zero, so this cannot be provoked by sampling; the guard is what this asserts, via
    // the standard-normal path over a large sample.
    RandomSource random{13U};
    for (int i = 0; i < 500'000; ++i) {
        ASSERT_TRUE(std::isfinite(random.next_normal()));
    }
}

TEST(TestRandom_Source, EmpiricalDiscreteRejectsEmptyAndMismatchedInput) {
    // Baseline finding B-14: only the sizes were checked, so two empty vectors read
    // values.back() — undefined behaviour.
    RandomSource random{17U};

    const std::vector<int> values{};
    const std::vector<double> cdf{};
    EXPECT_THROW(random.next_empirical_discrete(values, cdf), hgps::diag::InternalError);

    const std::vector<int> two{1, 2};
    const std::vector<double> one{1.0};
    EXPECT_THROW(random.next_empirical_discrete(two, one), hgps::diag::InternalError);

    // A rejected call must not consume a draw, or a reported programmer error would shift the
    // rest of the stream.
    EXPECT_EQ(0U, random.draw_count());
}

TEST(TestRandom_Source, EmpiricalDiscreteSelectsByCumulativeProbability) {
    RandomSource random{19U};

    const std::vector<int> values{10, 20, 30};
    const std::vector<double> cdf{0.2, 0.5, 1.0};

    std::map<int, int> counts;
    const int n = 60'000;
    for (int i = 0; i < n; ++i) {
        counts[random.next_empirical_discrete(values, cdf)] += 1;
    }

    EXPECT_NEAR(0.2, static_cast<double>(counts[10]) / n, 0.01);
    EXPECT_NEAR(0.3, static_cast<double>(counts[20]) / n, 0.01);
    EXPECT_NEAR(0.5, static_cast<double>(counts[30]) / n, 0.01);
}

TEST(TestRandom_Source, DerivedRunSeedsAreStableAndIndependentOfRunCount) {
    using hgps::rng::derive_job_seed;
    using hgps::rng::derive_run_seed;

    // Adding a trial run must not change the seeds of the runs before it: the value for
    // (master, index) is a pure function.
    const std::uint32_t master = 123456789U;
    const auto first = derive_run_seed(master, 0);
    const auto second = derive_run_seed(master, 1);

    EXPECT_EQ(first, derive_run_seed(master, 0));
    EXPECT_NE(first, second);
    EXPECT_NE(first, derive_run_seed(master + 1, 0));

    // Run seeds and job seeds never collide for the same index.
    for (std::uint32_t i = 0; i < 64; ++i) {
        EXPECT_NE(derive_run_seed(master, i), derive_job_seed(master, i));
    }
}
