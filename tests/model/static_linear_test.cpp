// The StaticLinear model's own arithmetic, tested where it can be tested without a population:
// the inverse Box-Cox transform, the ordered income CDF and its rank buckets, the max-subtracted
// softmax, and the two-stage logistic.
//
// Ports the intent of the baseline's `StaticLinearModelTest` and the parts of
// `IncomeStratumAdjustment.Test.cpp` that are about the split rather than about the adjustment,
// and adds the three cases the baseline has no test for and gets wrong: a softmax over logits big
// enough to overflow, a logistic term big enough to overflow, and a rank split where every value
// is the same.
#include "model/riskfactor/static_linear/static_linear_model.h"

#include "diagnostics/internal_error.h"
#include "model/population.h"

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::core::Gender;
using hgps::core::Identifier;
using hgps::model::Person;
using hgps::model::Population;
using hgps::model::StaticLinearModel;

const Identifier kIncome{"income"};

/// A population of `n` people whose incomes are the values given, in slot order.
Population population_with_incomes(const std::vector<double> &incomes) {
    Population population{incomes.size()};
    for (std::size_t i = 0; i < incomes.size(); ++i) {
        population[i].gender = i % 2 == 0 ? Gender::male : Gender::female;
        population[i].age = 40;
        population[i].risk_factors[kIncome] = incomes[i];
    }
    return population;
}

} // namespace

// --- the inverse Box-Cox transform ---------------------------------------------------------

TEST(StaticLinearBoxCox, LambdaOfOneIsTheIdentityShiftedByOne) {
    // (1 * x + 1)^(1/1) = x + 1.
    EXPECT_DOUBLE_EQ(3.0, StaticLinearModel::inverse_box_cox(2.0, 1.0));
    EXPECT_DOUBLE_EQ(1.0, StaticLinearModel::inverse_box_cox(0.0, 1.0));
}

TEST(StaticLinearBoxCox, LambdaOfZeroIsTheExponential) {
    EXPECT_DOUBLE_EQ(std::exp(1.5), StaticLinearModel::inverse_box_cox(1.5, 0.0));
    // Near-zero counts as zero: the value comes from a CSV and is never exactly 0.
    EXPECT_DOUBLE_EQ(std::exp(1.5), StaticLinearModel::inverse_box_cox(1.5, 1e-14));
}

TEST(StaticLinearBoxCox, AHalfLambdaSquaresTheShiftedArgument) {
    // (0.5 * 6 + 1)^2 = 16.
    EXPECT_DOUBLE_EQ(16.0, StaticLinearModel::inverse_box_cox(6.0, 0.5));
}

TEST(StaticLinearBoxCox, OutsideTheDomainItIsZeroRatherThanNotANumber) {
    // lambda * factor + 1 <= 0 has no real root for a fractional 1/lambda. A residual far enough
    // into the tail reaches this every run, and a NaN here would spread through the calibration
    // into everyone else's value.
    EXPECT_DOUBLE_EQ(0.0, StaticLinearModel::inverse_box_cox(-10.0, 0.5));
    EXPECT_DOUBLE_EQ(0.0, StaticLinearModel::inverse_box_cox(-2.0, 0.5));
}

TEST(StaticLinearBoxCox, AnOverflowIsZeroRatherThanInfinity) {
    EXPECT_DOUBLE_EQ(0.0, StaticLinearModel::inverse_box_cox(1e6, 0.0));
    EXPECT_TRUE(std::isfinite(StaticLinearModel::inverse_box_cox(1e300, 0.5)));
}

TEST(StaticLinearBoxCox, TheResultIsNeverNegative) {
    for (const double lambda : {-2.0, -0.5, 0.0, 0.25, 1.0, 3.0}) {
        for (const double factor : {-5.0, -1.0, -0.1, 0.0, 0.1, 1.0, 5.0}) {
            const double value = StaticLinearModel::inverse_box_cox(factor, lambda);
            EXPECT_GE(value, 0.0) << "lambda " << lambda << ", factor " << factor;
            EXPECT_TRUE(std::isfinite(value)) << "lambda " << lambda << ", factor " << factor;
        }
    }
}

// --- the softmax ---------------------------------------------------------------------------

TEST(StaticLinearSoftmax, EqualLogitsGiveEqualProbabilities) {
    const std::vector<double> logits{2.0, 2.0, 2.0, 2.0};
    const auto weights = StaticLinearModel::softmax(logits);

    ASSERT_EQ(4U, weights.size());
    for (const double weight : weights) {
        EXPECT_DOUBLE_EQ(0.25, weight);
    }
}

TEST(StaticLinearSoftmax, TheProbabilitiesSumToOneAndKeepTheirOrder) {
    const std::vector<double> logits{-1.0, 0.0, 2.5, 1.0};
    const auto weights = StaticLinearModel::softmax(logits);

    double total = 0.0;
    for (const double weight : weights) {
        total += weight;
    }
    EXPECT_NEAR(1.0, total, 1e-12);

    // The largest logit gets the largest probability.
    EXPECT_GT(weights[2], weights[3]);
    EXPECT_GT(weights[3], weights[1]);
    EXPECT_GT(weights[1], weights[0]);
}

TEST(StaticLinearSoftmax, AddingAConstantToEveryLogitChangesNothing) {
    const std::vector<double> logits{-1.0, 0.0, 2.5, 1.0};
    const std::vector<double> shifted{99.0, 100.0, 102.5, 101.0};

    const auto plain = StaticLinearModel::softmax(logits);
    const auto moved = StaticLinearModel::softmax(shifted);

    ASSERT_EQ(plain.size(), moved.size());
    for (std::size_t i = 0; i < plain.size(); ++i) {
        EXPECT_NEAR(plain[i], moved[i], 1e-12);
    }
}

TEST(StaticLinearSoftmax, LogitsLargeEnoughToOverflowStillGiveProbabilities) {
    // exp(800) is infinity in a double. The baseline exponentiates the raw logits and then
    // divides infinity by infinity, so its probabilities are NaN and its CDF never reaches the
    // draw — which throws "failed to initialise categorical income category". Subtracting the
    // maximum first is exact for these inputs.
    const std::vector<double> logits{800.0, 799.0, 10.0};
    const auto weights = StaticLinearModel::softmax(logits);

    ASSERT_EQ(3U, weights.size());
    double total = 0.0;
    for (const double weight : weights) {
        EXPECT_TRUE(std::isfinite(weight));
        EXPECT_GE(weight, 0.0);
        total += weight;
    }
    EXPECT_NEAR(1.0, total, 1e-12);
    EXPECT_NEAR(1.0 / (1.0 + std::exp(-1.0)), weights[0], 1e-12);
}

TEST(StaticLinearSoftmax, VeryNegativeLogitsDoNotUnderflowToNothing) {
    const std::vector<double> logits{-800.0, -801.0};
    const auto weights = StaticLinearModel::softmax(logits);

    EXPECT_NEAR(1.0 / (1.0 + std::exp(-1.0)), weights[0], 1e-12);
    EXPECT_NEAR(1.0 - weights[0], weights[1], 1e-12);
}

TEST(StaticLinearSoftmax, AnEmptySetOfLogitsIsAProgrammerError) {
    EXPECT_THROW(StaticLinearModel::softmax(std::vector<double>{}), hgps::diag::InternalError);
}

// --- the rank buckets and the income CDF ---------------------------------------------------

TEST(StaticLinearIncomeSplit, TheBucketsAreEqualInPopulationNotInWidth) {
    // Ten people, four buckets: 2, 3, 2, 3 — floor(rank * 4 / 10). The values are deliberately
    // skewed, so equal-width thresholds would put seven of them in the first bucket.
    const auto population =
        population_with_incomes({1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 50.0, 200.0, 900.0});
    const auto buckets = StaticLinearModel::equal_rank_buckets(population, 4);

    ASSERT_EQ(10U, buckets.size());

    std::vector<int> counts(4, 0);
    for (const auto &[index, bucket] : buckets) {
        ASSERT_LT(bucket, 4U);
        ++counts[bucket];
    }
    EXPECT_EQ(std::vector<int>({3, 2, 3, 2}), counts);
}

TEST(StaticLinearIncomeSplit, ABucketIsNeverEmptyEvenWhenEveryValueIsTheSame) {
    // The reason the split is by rank and not by threshold. With thresholds, a population whose
    // income is a single repeated value puts everybody at or below every cut point, so the top
    // bucket is empty and a whole income stratum has nobody in it.
    const auto population = population_with_incomes(std::vector<double>(20, 7.0));
    const auto buckets = StaticLinearModel::equal_rank_buckets(population, 5);

    std::vector<int> counts(5, 0);
    for (const auto &[index, bucket] : buckets) {
        ++counts[bucket];
    }
    for (const int count : counts) {
        EXPECT_EQ(4, count);
    }
}

TEST(StaticLinearIncomeSplit, TiesAreBrokenBySlotSoTheSplitIsNotAnAccidentOfTheSort) {
    const auto population = population_with_incomes(std::vector<double>(8, 3.0));
    const auto buckets = StaticLinearModel::equal_rank_buckets(population, 2);

    // Every income is equal, so rank is slot order: the first four are bucket 0.
    for (const auto &[index, bucket] : buckets) {
        EXPECT_EQ(index < 4 ? 0U : 1U, bucket) << "slot " << index;
    }
}

TEST(StaticLinearIncomeSplit, PeopleWithoutAnIncomeAreLeftOut) {
    Population population{4};
    for (std::size_t i = 0; i < 4; ++i) {
        population[i].age = 40;
        population[i].gender = Gender::male;
    }
    population[0].risk_factors[kIncome] = 1.0;
    population[2].risk_factors[kIncome] = 2.0;

    const auto buckets = StaticLinearModel::equal_rank_buckets(population, 2);
    ASSERT_EQ(2U, buckets.size());
    EXPECT_EQ(0U, buckets[0].first);
    EXPECT_EQ(2U, buckets[1].first);
}

TEST(StaticLinearIncomeSplit, InactivePeopleAreLeftOut) {
    auto population = population_with_incomes({1.0, 2.0, 3.0, 4.0});
    population[1].die(2020);
    population[3].emigrate(2020);

    const auto buckets = StaticLinearModel::equal_rank_buckets(population, 2);
    ASSERT_EQ(2U, buckets.size());
    EXPECT_EQ(0U, buckets[0].first);
    EXPECT_EQ(2U, buckets[1].first);
}

TEST(StaticLinearIncomeSplit, NoBucketsMeansNoAssignment) {
    const auto population = population_with_incomes({1.0, 2.0});
    EXPECT_TRUE(StaticLinearModel::equal_rank_buckets(population, 0).empty());
}

TEST(StaticLinearIncomeSplit, TheThresholdsAreTheOrderedQuantilesOfTheDistribution) {
    // 0..99. The i-th of four cut points is the value at rank round(99 * (i+1)/4).
    std::vector<double> incomes;
    incomes.reserve(100);
    for (int i = 0; i < 100; ++i) {
        incomes.push_back(static_cast<double>(i));
    }
    const auto population = population_with_incomes(incomes);

    const auto thresholds = StaticLinearModel::income_percentile_thresholds(population, 4);
    ASSERT_EQ(3U, thresholds.size());
    EXPECT_DOUBLE_EQ(25.0, thresholds[0]);
    EXPECT_DOUBLE_EQ(50.0, thresholds[1]);
    EXPECT_DOUBLE_EQ(74.0, thresholds[2]);

    // Ascending, always: a threshold set that is not ordered would put a person in two buckets.
    for (std::size_t i = 1; i < thresholds.size(); ++i) {
        EXPECT_LE(thresholds[i - 1], thresholds[i]);
    }
}

TEST(StaticLinearIncomeSplit, ThreeAndFiveCategoriesUseTheSameRuleAsFour) {
    std::vector<double> incomes;
    for (int i = 0; i < 100; ++i) {
        incomes.push_back(static_cast<double>(i));
    }
    const auto population = population_with_incomes(incomes);

    EXPECT_EQ(2U, StaticLinearModel::income_percentile_thresholds(population, 3).size());
    EXPECT_EQ(4U, StaticLinearModel::income_percentile_thresholds(population, 5).size());

    // The baseline special-cases three and four with hand-written index arithmetic and falls back
    // to a general formula for anything else. One rule here, so five is not a different model.
    const auto tertiles = StaticLinearModel::income_percentile_thresholds(population, 3);
    EXPECT_DOUBLE_EQ(33.0, tertiles[0]);
    EXPECT_DOUBLE_EQ(66.0, tertiles[1]);
}

TEST(StaticLinearIncomeSplit, FewerThanTwoBucketsHasNoCutPoints) {
    const auto population = population_with_incomes({1.0, 2.0, 3.0});
    EXPECT_TRUE(StaticLinearModel::income_percentile_thresholds(population, 1).empty());
}

TEST(StaticLinearIncomeSplit, AnEmptyPopulationIsAProgrammerErrorRatherThanAnEmptyAnswer) {
    Population population{0};
    EXPECT_THROW(StaticLinearModel::income_percentile_thresholds(population, 4),
                 hgps::diag::InternalError);
}
