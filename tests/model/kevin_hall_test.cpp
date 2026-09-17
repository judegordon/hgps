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
