// The Kevin Hall energy balance's arithmetic, tested against the physiology it encodes rather
// than against a recorded output.
//
// The baseline has no test for any of this: its `KevinHallHeight` and `KevinHallWeightQuantiles`
// suites are about loading, and every one of them skips. What is checked here is that the
// equations behave the way the model they come from says they should — glycogen tracks
// carbohydrate, fluid tracks sodium, a bigger body costs more to run, and a person in energy
// balance stays where they are.
#include "model/riskfactor/kevin_hall/kevin_hall_model.h"

#include "diagnostics/internal_error.h"

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::core::Gender;
using hgps::model::KevinHallModel;

} // namespace

// --- glycogen -------------------------------------------------------------------------------

TEST(KevinHallPhysiology, GlycogenIsUnchangedWhenCarbohydrateIntakeIsUnchanged) {
    EXPECT_DOUBLE_EQ(0.5, KevinHallModel::compute_glycogen(300.0, 300.0, 0.5));
}

TEST(KevinHallPhysiology, GlycogenFollowsTheSquareRootOfCarbohydrateIntake) {
    // G = sqrt(CI / k) with k fixed by the initial pair, so four times the carbohydrate is twice
    // the glycogen.
    EXPECT_DOUBLE_EQ(1.0, KevinHallModel::compute_glycogen(1200.0, 300.0, 0.5));
    EXPECT_DOUBLE_EQ(0.25, KevinHallModel::compute_glycogen(75.0, 300.0, 0.5));
}

TEST(KevinHallPhysiology, GlycogenNeedsAPositiveStartingPoint) {
    EXPECT_THROW(KevinHallModel::compute_glycogen(300.0, 300.0, 0.0), hgps::diag::InternalError);
    EXPECT_THROW(KevinHallModel::compute_glycogen(300.0, 0.0, 0.5), hgps::diag::InternalError);
}

// --- extracellular fluid --------------------------------------------------------------------

TEST(KevinHallPhysiology, FluidIsUnchangedWhenNeitherSodiumNorCarbohydrateMoves) {
    EXPECT_DOUBLE_EQ(15.0,
                     KevinHallModel::compute_extracellular_fluid(0.0, 300.0, 300.0, 15.0));
}

TEST(KevinHallPhysiology, MoreSodiumMeansMoreFluid) {
    const double more = KevinHallModel::compute_extracellular_fluid(3000.0, 300.0, 300.0, 15.0);
    EXPECT_GT(more, 15.0);
    // 3000 mg over 3000 mg/L is one litre, one kilogram.
    EXPECT_DOUBLE_EQ(16.0, more);
}

TEST(KevinHallPhysiology, LessCarbohydrateMeansLessFluid) {
    // Halving the carbohydrate releases 4000 * 0.5 mg of sodium, so the fluid falls by
    // 2000 / 3000 of a litre.
    const double fluid = KevinHallModel::compute_extracellular_fluid(0.0, 150.0, 300.0, 15.0);
    EXPECT_NEAR(15.0 - 2000.0 / 3000.0, fluid, 1e-12);
    EXPECT_LT(fluid, 15.0);
}

TEST(KevinHallPhysiology, FluidNeedsAPositiveStartingCarbohydrateIntake) {
    EXPECT_THROW(KevinHallModel::compute_extracellular_fluid(0.0, 300.0, 0.0, 15.0),
                 hgps::diag::InternalError);
}

// --- the energy cost per kilogram -------------------------------------------------------------

TEST(KevinHallPhysiology, TheEnergyCostPerKiloRisesWithThePhysicalActivityLevel) {
    const double sedentary = KevinHallModel::compute_delta(40, Gender::male, 1.4, 80.0, 175.0);
    const double active = KevinHallModel::compute_delta(40, Gender::male, 2.2, 80.0, 175.0);
    EXPECT_GT(active, sedentary);
}

TEST(KevinHallPhysiology, AtRestTheEnergyCostPerKiloIsNegative) {
    // delta is what is left of total expenditure once the resting rate and the thermic effect are
    // taken out, so a person who does nothing at all (PAL below 1/(1 - beta_TEF)) has a negative
    // residual: the resting rate already accounts for more than they spend.
    EXPECT_LT(KevinHallModel::compute_delta(40, Gender::male, 1.0, 80.0, 175.0), 0.0);
}

TEST(KevinHallPhysiology, MenAndWomenOfTheSameSizeDifferByTheMifflinStJeorConstant) {
    const double male = KevinHallModel::compute_delta(40, Gender::male, 1.6, 80.0, 175.0);
    const double female = KevinHallModel::compute_delta(40, Gender::female, 1.6, 80.0, 175.0);

    // The rate differs by (5 - (-161)) kcal = 166 kcal, converted and spread over the body.
    const double expected = ((1.0 - KevinHallModel::kBetaTef) * 1.6 - 1.0) * 166.0 * 4.184 / 80.0;
    EXPECT_NEAR(expected, male - female, 1e-9);
}

TEST(KevinHallPhysiology, AZeroBodyWeightIsAProgrammerError) {
    EXPECT_THROW(KevinHallModel::compute_delta(40, Gender::male, 1.6, 0.0, 175.0),
                 hgps::diag::InternalError);
}

// --- energy expenditure -----------------------------------------------------------------------

TEST(KevinHallPhysiology, ExpenditureRisesWithIntake) {
    const auto at = [](double intake) {
        return KevinHallModel::compute_expenditure(80.0, 20.0, 40.0, intake, 1000.0, 20.0, 0.1);
    };
    EXPECT_GT(at(12000.0), at(8000.0));
}

TEST(KevinHallPhysiology, ExpenditureRisesWithLeanTissueFasterThanWithFat) {
    // gamma_lean is 92 kJ/kg/day against gamma_fat's 13: lean tissue is metabolically expensive
    // and fat is nearly free, which is why losing lean tissue lowers the resting rate.
    const auto at = [](double fat, double lean) {
        return KevinHallModel::compute_expenditure(80.0, fat, lean, 10000.0, 1000.0, 20.0, 0.1);
    };
    const double extra_fat = at(21.0, 40.0) - at(20.0, 40.0);
    const double extra_lean = at(20.0, 41.0) - at(20.0, 40.0);
    EXPECT_GT(extra_lean, extra_fat);
    EXPECT_NEAR(KevinHallModel::kGammaLean / KevinHallModel::kGammaFat, extra_lean / extra_fat,
                1e-9);
}

// --- the whole thing hangs together -----------------------------------------------------------

TEST(KevinHallPhysiology, APersonInBalanceStaysWhereTheyAre) {
    // The state initialiser chooses the intercept so that expenditure equals intake in the first
    // year. That is the property the model rests on: a person whose diet does not change should
    // not drift, and if the intercept were chosen any other way everybody would gain or lose
    // weight on the first step for no reason.
    constexpr double weight = 80.0;
    constexpr double fat = 20.0;
    constexpr double lean = 80.0 - 20.0 - 0.8 - 2.16 - 13.16;
    constexpr double intake = 10000.0;

    const double delta = KevinHallModel::compute_delta(40, Gender::male, 1.6, weight, 175.0);
    const double intercept =
        intake - (KevinHallModel::kGammaFat * fat + KevinHallModel::kGammaLean * lean +
                  delta * weight);

    const double c = 10.4 * KevinHallModel::kRhoLean / KevinHallModel::kRhoFat;
    const double p = c / (c + fat);
    const double partition =
        p * KevinHallModel::kEtaLean / KevinHallModel::kRhoLean +
        (1.0 - p) * KevinHallModel::kEtaFat / KevinHallModel::kRhoFat;

    const double expenditure = KevinHallModel::compute_expenditure(weight, fat, lean, intake,
                                                                    intercept, delta, partition);

    // Equal to the intake, up to the thermic and adaptive constants the expenditure formula adds
    // and the initialiser does not.
    EXPECT_NEAR(intake, expenditure, 1.0);
}

// --- the edge of the model's domain -----------------------------------------------------------
//
// `p = C / (C + F)` is a coefficient of the relaxation the yearly step solves, and it has a pole
// at `F = -C`. Everything below is about what happens on either side of it, and the numbers in
// the last three tests are the ones seed 80 of `KevinHall_FINCH` actually produced
// (docs/findings/seed-80.md, ADR 0049).

namespace {

/// The partition coefficient's pole: C = 10.4 · rho_lean / rho_fat, about -2.001 kg of body fat.
constexpr double kPole = -10.4 * KevinHallModel::kRhoLean / KevinHallModel::kRhoFat;

} // namespace

TEST(KevinHallDomain, AStepThatStaysAboveZeroIsLeftAlone) {
    // The ordinary case, and the one that must not change: a relaxation from 25 kg toward 24 kg
    // that covers half the distance is 24.5 kg, and the guard has no opinion about it.
    const auto step = KevinHallModel::bounded_step(24.0, 25.0, 58.0, 60.0, 500.0, 0.5);

    EXPECT_FALSE(step.bounded.has_value());
    EXPECT_DOUBLE_EQ(24.5, step.fat);
    EXPECT_DOUBLE_EQ(59.0, step.lean);
}

TEST(KevinHallDomain, ArrivingExactlyAtZeroIsNotCrossingZero) {
    // F* = 0 makes every later year's p exactly 1, which is finite, so a body that has arrived at
    // no fat is at the boundary rather than past it. Bounding here would raise a warning every
    // year for the rest of that person's life and change nothing.
    const auto step = KevinHallModel::bounded_step(0.0, 0.0, 40.0, 42.0, 300.0, 0.25);

    EXPECT_FALSE(step.bounded.has_value());
    EXPECT_DOUBLE_EQ(0.0, step.fat);
    EXPECT_DOUBLE_EQ(40.5, step.lean);
}

TEST(KevinHallDomain, AStepBelowZeroStopsAtZeroAndSaysSo) {
    // `reached` is exp(-365/tau): 1 is no movement at all and 0 is the whole way to the steady
    // state, so a *smaller* one is a longer year. A steady state of -5 kg from a starting 5 kg
    // reaches zero at exp(-t*/tau) = F*/(F* - F0) = -5 / -10 = 0.5, and a year that goes further
    // than that — here to 0.3, which would have left -2 kg of fat — is stopped there. Lean
    // tissue is taken to the same instant: 30 - (30 - 50)·0.5.
    const auto step = KevinHallModel::bounded_step(-5.0, 5.0, 30.0, 50.0, 200.0, 0.3);

    EXPECT_DOUBLE_EQ(-2.0, -5.0 - (-5.0 - 5.0) * 0.3);  // what it would have been
    ASSERT_TRUE(step.bounded.has_value());
    EXPECT_EQ(KevinHallModel::BoundedReason::body_fat_below_zero, *step.bounded);
    EXPECT_DOUBLE_EQ(0.0, step.fat);
    EXPECT_DOUBLE_EQ(40.0, step.lean);
}

TEST(KevinHallDomain, AYearThatStopsShortOfTheCrossingIsLeftAlone) {
    // The same person and the same steady state, with a year that only gets 70% of the way
    // there. Body fat is still positive, so nothing is bounded — the guard fires on the
    // trajectory leaving the domain, not on the steady state being outside it.
    const auto step = KevinHallModel::bounded_step(-5.0, 5.0, 30.0, 50.0, 200.0, 0.7);

    EXPECT_FALSE(step.bounded.has_value());
    EXPECT_DOUBLE_EQ(2.0, step.fat);
}

TEST(KevinHallDomain, TheBoundedLeanTissueIsTheSameSolutionAtTheSameInstant) {
    // The point of stopping at the crossing rather than freezing the year: lean tissue is not
    // held at its starting value, it is taken to where its own half of the solution is when body
    // fat reaches zero. With F* = -1 and F0 = 3 the crossing is at 0.25.
    const auto step = KevinHallModel::bounded_step(-1.0, 3.0, 20.0, 60.0, 400.0, 0.1);

    ASSERT_TRUE(step.bounded.has_value());
    EXPECT_DOUBLE_EQ(0.0, step.fat);
    EXPECT_DOUBLE_EQ(20.0 - (20.0 - 60.0) * 0.25, step.lean);
    EXPECT_DOUBLE_EQ(30.0, step.lean);
}

TEST(KevinHallDomain, ANegativeTimeConstantTakesNoStepAtAll) {
    // A negative tau turns the year's step from a relaxation *toward* the steady state into an
    // exponential flight *away* from it. There is no instant inside the year at which the model
    // is still describing anything, so nothing is integrated and the composition is unchanged.
    const auto step = KevinHallModel::bounded_step(-2.4, -2.2, 38.0, 37.5, -0.559, 2.29e283);

    ASSERT_TRUE(step.bounded.has_value());
    EXPECT_EQ(KevinHallModel::BoundedReason::step_is_not_a_relaxation, *step.bounded);
    EXPECT_DOUBLE_EQ(-2.2, step.fat);
    EXPECT_DOUBLE_EQ(37.5, step.lean);
}

TEST(KevinHallDomain, ANonFiniteStepTakesNoStepAtAll) {
    const auto step = KevinHallModel::bounded_step(
        1.0, 2.0, 3.0, 4.0, 100.0, std::numeric_limits<double>::infinity());

    ASSERT_TRUE(step.bounded.has_value());
    EXPECT_EQ(KevinHallModel::BoundedReason::step_is_not_a_relaxation, *step.bounded);
    EXPECT_DOUBLE_EQ(2.0, step.fat);
    EXPECT_DOUBLE_EQ(4.0, step.lean);
}

TEST(KevinHallDomain, ThePoleIsWhereTheModelSaysItIs) {
    // Not a round number and not a constant anybody can change independently: it is
    // 10.4 · rho_lean / rho_fat, and a body fat mass below it inverts the partition coefficient.
    EXPECT_NEAR(-2.001012658227848, kPole, 1e-15);

    const double c = -kPole;
    const auto p_at = [c](double fat) { return c / (c + fat); };

    EXPECT_GT(p_at(0.0), 0.0);
    EXPECT_DOUBLE_EQ(1.0, p_at(0.0));
    EXPECT_GT(p_at(kPole + 0.1), 0.0);   // just above the pole: positive, and very large
    EXPECT_LT(p_at(kPole - 0.1), 0.0);   // just below it: negative, which is meaningless
    EXPECT_LT(p_at(-2.2324094172489493), 0.0);
}

// --- seed 80 of KevinHall_FINCH, pinned -------------------------------------------------------

TEST(KevinHallPhysiology, TheSeed80TrajectoryIsWhatTheTraceRecorded) {
    // Person 1222, male, in simulated 2030: the year their body fat goes negative and the first
    // value in the whole trajectory that cannot exist. Traced from the run, and reproduced
    // digit for digit by the baseline's own statements (docs/findings/seed-80.md §3).
    constexpr double steady_fat = -3.0670836989773984;
    constexpr double fat_0 = 5.8617563101306471;
    constexpr double steady_lean = 35.821273667675811;
    constexpr double lean_0 = 51.662931215642693;
    constexpr double tau = 154.66107471082029;
    constexpr double reached = 0.0944203064178748;

    // What the baseline computes, and what this build computed before the guard.
    EXPECT_NEAR(-2.2240198893612368, steady_fat - (steady_fat - fat_0) * reached, 1e-13);

    // What it computes now: the step stops where body fat reaches zero.
    const auto step = KevinHallModel::bounded_step(steady_fat, fat_0, steady_lean, lean_0, tau,
                                                    reached);
    ASSERT_TRUE(step.bounded.has_value());
    EXPECT_EQ(KevinHallModel::BoundedReason::body_fat_below_zero, *step.bounded);
    EXPECT_DOUBLE_EQ(0.0, step.fat);

    // The crossing is at exp(-t*/tau) = F*/(F* - F0), which is inside the year — so the person
    // really does run out of fat partway through 2030 rather than at the end of it.
    const double crossing = steady_fat / (steady_fat - fat_0);
    EXPECT_GT(crossing, reached);
    EXPECT_LT(crossing, 1.0);
    EXPECT_DOUBLE_EQ(steady_lean - (steady_lean - lean_0) * crossing, step.lean);
}

TEST(KevinHallPhysiology, PastThePoleOneYearIsSixHundredEFoldings) {
    // Person 1222 in 2031, entering with the -2.2324 kg of fat the unguarded 2030 step left.
    // This is the arithmetic the run died on, and it is arithmetic rather than a bug: every
    // constant here is the model's own.
    constexpr double fat_0 = -2.2324094172489493;
    constexpr double delta = 68.166056768948593;

    const double c = 10.4 * KevinHallModel::kRhoLean / KevinHallModel::kRhoFat;
    const double p = c / (c + fat_0);
    EXPECT_NEAR(-8.6475396919685217, p, 1e-12);

    const double partition = p * KevinHallModel::kEtaLean / KevinHallModel::kRhoLean +
                             (1.0 - p) * KevinHallModel::kEtaFat / KevinHallModel::kRhoFat;
    const double determinant = (KevinHallModel::kGammaFat + delta) * (1.0 - p) *
                                   KevinHallModel::kRhoLean +
                               (KevinHallModel::kGammaLean + delta) * p * KevinHallModel::kRhoFat;
    EXPECT_LT(determinant, 0.0);

    const double tau = KevinHallModel::kRhoLean * KevinHallModel::kRhoFat * (1.0 + partition) /
                       determinant;
    EXPECT_NEAR(-0.55942178144697796, tau, 1e-12);

    // Half a day of time constant, run backwards for a year.
    EXPECT_GT(std::exp(-365.0 / tau), 1e283);

    // And the guard refuses to take a step whose time constant is negative at all, so this
    // person's 2031 never happens: they entered 2031 with zero fat instead.
    const auto step = KevinHallModel::bounded_step(-2.4, fat_0, 38.0, 37.4, tau,
                                                    std::exp(-365.0 / tau));
    ASSERT_TRUE(step.bounded.has_value());
    EXPECT_EQ(KevinHallModel::BoundedReason::step_is_not_a_relaxation, *step.bounded);
    EXPECT_TRUE(std::isfinite(step.fat));
    EXPECT_TRUE(std::isfinite(step.lean));
}
