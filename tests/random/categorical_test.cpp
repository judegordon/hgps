// New here. Determinism contract clause D4 and baseline finding B-05: the baseline built an
// income CDF by iterating an std::unordered_map, so the same seed could assign a different income
// category on a different standard library.
#include "core/income_category_layout.h"
#include "diagnostics/internal_error.h"
#include "random/categorical.h"
#include "random/source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

using hgps::rng::Categorical;
using hgps::rng::RandomSource;

TEST(TestRandom_Categorical, SamplesInProportionToItsWeights) {
    const std::vector<std::string> values{"a", "b", "c"};
    const std::vector<double> weights{1.0, 3.0, 6.0};
    const auto dist = Categorical<std::string>::from_weights(values, weights);

    ASSERT_EQ(3U, dist.size());
    EXPECT_NEAR(0.1, dist.probability(0), 1e-12);
    EXPECT_NEAR(0.3, dist.probability(1), 1e-12);
    EXPECT_NEAR(0.6, dist.probability(2), 1e-12);
    EXPECT_DOUBLE_EQ(1.0, dist.cdf().back());

    RandomSource random{101U};
    std::map<std::string, int> counts;
    const int n = 60'000;
    for (int i = 0; i < n; ++i) {
        counts[dist.sample(random)] += 1;
    }

    EXPECT_NEAR(0.1, static_cast<double>(counts["a"]) / n, 0.01);
    EXPECT_NEAR(0.3, static_cast<double>(counts["b"]) / n, 0.01);
    EXPECT_NEAR(0.6, static_cast<double>(counts["c"]) / n, 0.01);
}

TEST(TestRandom_Categorical, TheOrderOfTheInputDecidesWhichValueADrawSelects) {
    // The property the baseline lost by iterating an unordered container: two distributions over
    // the same value set in different orders must disagree about what a given draw selects, so
    // the order has to be stated rather than inherited from a container.
    const std::vector<int> ascending{1, 2, 3};
    const std::vector<int> descending{3, 2, 1};
    const std::vector<double> weights{0.5, 0.3, 0.2};

    const auto a = Categorical<int>::from_weights(ascending, weights);
    const auto b = Categorical<int>::from_weights(descending, weights);

    RandomSource left{7U};
    RandomSource right{7U};

    int disagreements = 0;
    for (int i = 0; i < 1'000; ++i) {
        if (a.sample(left) != b.sample(right)) {
            ++disagreements;
        }
    }

    EXPECT_GT(disagreements, 0);
    EXPECT_EQ(ascending, std::vector<int>(a.values().begin(), a.values().end()));
}

TEST(TestRandom_Categorical, SameSeedSameSequence) {
    const std::vector<int> values{10, 20, 30, 40};
    const std::vector<double> weights{1.0, 1.0, 1.0, 1.0};
    const auto dist = Categorical<int>::from_weights(values, weights);

    RandomSource a{2718U};
    RandomSource b{2718U};
    for (int i = 0; i < 500; ++i) {
        ASSERT_EQ(dist.sample(a), dist.sample(b));
    }
}

TEST(TestRandom_Categorical, RejectsDegenerateWeights) {
    const std::vector<int> values{1, 2};

    EXPECT_THROW(Categorical<int>::from_weights(values, std::vector<double>{1.0}),
                 hgps::diag::InternalError);
    EXPECT_THROW(Categorical<int>::from_weights(values, std::vector<double>{0.0, 0.0}),
                 hgps::diag::InternalError);
    EXPECT_THROW(Categorical<int>::from_weights(values, std::vector<double>{-1.0, 2.0}),
                 hgps::diag::InternalError);
    EXPECT_THROW(
        Categorical<int>::from_weights(values, std::vector<double>{std::nan(""), 1.0}),
        hgps::diag::InternalError);
    EXPECT_THROW(Categorical<int>::from_weights(std::vector<int>{}, std::vector<double>{}),
                 hgps::diag::InternalError);
}

TEST(TestRandom_Categorical, AZeroWeightValueIsNeverSelected) {
    const std::vector<int> values{1, 2, 3};
    const std::vector<double> weights{0.0, 1.0, 0.0};
    const auto dist = Categorical<int>::from_weights(values, weights);

    RandomSource random{31U};
    for (int i = 0; i < 10'000; ++i) {
        ASSERT_EQ(2, dist.sample(random));
    }
}

TEST(TestRandom_Categorical, BuildsFromPairsAndFromAnOrderedMap) {
    const std::vector<std::pair<int, double>> pairs{{5, 0.25}, {6, 0.75}};
    const auto from_pairs = Categorical<int>::from_pairs(pairs);
    EXPECT_NEAR(0.25, from_pairs.probability(0), 1e-12);

    const std::map<int, double> ordered{{9, 1.0}, {7, 3.0}};
    const auto from_map = Categorical<int>::from_ordered_map(ordered);
    // std::map iterates by key, so 7 comes first and its order is stated by the container's
    // contract rather than left unspecified.
    ASSERT_EQ(2U, from_map.size());
    EXPECT_EQ(7, from_map.values()[0]);
    EXPECT_NEAR(0.75, from_map.probability(0), 1e-12);
}

TEST(TestRandom_Categorical, IncomeCategoriesSampleInLayoutOrder) {
    using namespace hgps::core;

    const auto layout = income_category_layout_from_config("4");
    const std::vector<double> weights{0.4, 0.3, 0.2, 0.1};
    const auto dist = Categorical<Income>::from_weights(layout.ordered_strata(), weights);

    ASSERT_EQ(4U, dist.size());
    EXPECT_EQ(Income::low, dist.values()[0]);
    EXPECT_EQ(Income::high, dist.values()[3]);

    RandomSource random{53U};
    std::map<Income, int> counts;
    const int n = 40'000;
    for (int i = 0; i < n; ++i) {
        counts[dist.sample(random)] += 1;
    }
    EXPECT_NEAR(0.4, static_cast<double>(counts[Income::low]) / n, 0.01);
    EXPECT_NEAR(0.1, static_cast<double>(counts[Income::high]) / n, 0.01);
}
