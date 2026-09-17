// The five age-banded intervention scenarios.
//
// Ports the intent of the baseline's `Scenario.Test.cpp` coverage of `MarketingPolicyScenario`,
// `MarketingDynamicScenario`, `FiscalPolicyScenario`, `PhysicalActivityScenario` and
// `FoodLabellingScenario`, and adds the property none of them checks: that a person who moves from
// one age band to the next ends on the *new* band's effect rather than on the sum of the two.
#include "sim/scenario.h"

#include "diagnostics/internal_error.h"
#include "random/source.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::config::InterventionSpec;
using hgps::config::PolicyAdjustment;
using hgps::config::PolicyImpact;
using hgps::core::Gender;
using hgps::core::Identifier;
using hgps::model::Person;
using hgps::sim::create_intervention_scenario;

const Identifier kBmi{"bmi"};

PolicyImpact band(double value, unsigned int from, std::optional<unsigned int> to) {
    return PolicyImpact{.risk_factor = "bmi",
                        .impact_value = value,
                        .from_age = from,
                        .to_age = to};
}

/// Three ordered bands: children, adolescents, adults — the shape all the upstream examples use.
InterventionSpec three_bands(const std::string &identifier) {
    InterventionSpec spec;
    spec.identifier = identifier;
    spec.active_period = hgps::config::PolicyPeriod{.start_time = 2020, .finish_time = 2050};
    spec.impacts = {band(-0.12, 5, 12), band(-0.31, 13, 18), band(-0.16, 19, std::nullopt)};
    return spec;
}

Person person_aged(unsigned int age, std::size_t id = 1) {
    Person person{Gender::male, id};
    person.age = age;
    person.risk_factors[kBmi] = 25.0;
    return person;
}

hgps::rng::RandomSource source(std::uint32_t seed = 7) { return hgps::rng::RandomSource{seed}; }

} // namespace

// --- what they all share ----------------------------------------------------------------------

TEST(InterventionScenarios, EveryUpstreamIdentifierCanBeBuilt) {
    // The six the config loader accepts. A seventh is a programmer error, because the loader has
    // already rejected it (ADR 0021).
    for (const auto &identifier :
         {"simple", "marketing", "dynamic_marketing", "fiscal", "physical_activity",
          "food_labelling"}) {
        auto spec = three_bands(identifier);
        spec.impact_type = "absolute";
        if (std::string{identifier} == "dynamic_marketing") {
            spec.dynamics = {0.5, 0.1, 0.2};
        }
        if (std::string{identifier} == "fiscal") {
            spec.impact_type = "pessimist";
        }
        if (std::string{identifier} == "physical_activity") {
            spec.coverage_rates = {0.5};
        }
        if (std::string{identifier} == "food_labelling") {
            spec.coverage_rates = {0.3, 0.6};
            spec.coverage_cutoff_time = 5;
            spec.child_cutoff_age = 18;
            spec.coefficients = {0.1, 0.2, 0.3, 0.4};
            spec.adjustments = {PolicyAdjustment{.risk_factor = "bmi", .value = 0.5}};
        }

        EXPECT_NO_THROW(create_intervention_scenario(spec)) << identifier;
    }
}

TEST(InterventionScenarios, AnUnknownIdentifierIsAProgrammerError) {
    auto spec = three_bands("subsidy");
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);
}

TEST(InterventionScenarios, NothingHappensOutsideTheActivePeriod) {
    auto spec = three_bands("marketing");
    auto scenario = create_intervention_scenario(spec);
    auto random = source();
    auto person = person_aged(30);

    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2019, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2051, kBmi, 25.0));
}

TEST(InterventionScenarios, NothingHappensToAFactorThePolicyDoesNotName) {
    auto spec = three_bands("marketing");
    auto scenario = create_intervention_scenario(spec);
    auto random = source();
    auto person = person_aged(30);

    EXPECT_DOUBLE_EQ(9.0, scenario->apply(random, person, 2030, Identifier{"energy"}, 9.0));
}

TEST(InterventionScenarios, NothingHappensBelowTheFirstBand) {
    auto spec = three_bands("marketing");
    auto scenario = create_intervention_scenario(spec);
    auto random = source();
    auto person = person_aged(4);

    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2030, kBmi, 25.0));
}

TEST(InterventionScenarios, OverlappingOrUnorderedBandsAreRejected) {
    auto spec = three_bands("marketing");
    spec.impacts = {band(-0.12, 5, 20), band(-0.31, 13, 18), band(-0.16, 19, std::nullopt)};
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);

    spec.impacts = {band(-0.12, 5, 4), band(-0.31, 13, 18), band(-0.16, 19, std::nullopt)};
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);
}

TEST(InterventionScenarios, TooFewBandsIsRejected) {
    auto spec = three_bands("marketing");
    spec.impacts.pop_back();
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);
}

// --- marketing --------------------------------------------------------------------------------

TEST(MarketingScenario, TheFirstExposureIsTheWholeBandEffect) {
    auto scenario = create_intervention_scenario(three_bands("marketing"));
    auto random = source();
    auto person = person_aged(30);

    EXPECT_DOUBLE_EQ(25.0 - 0.16, scenario->apply(random, person, 2030, kBmi, 25.0));
}

TEST(MarketingScenario, APersonIsNotAffectedTwiceInTheSameBand) {
    auto scenario = create_intervention_scenario(three_bands("marketing"));
    auto random = source();
    auto person = person_aged(30);

    EXPECT_DOUBLE_EQ(25.0 - 0.16, scenario->apply(random, person, 2030, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2031, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2032, kBmi, 25.0));
}

TEST(MarketingScenario, MovingUpABandAppliesOnlyTheDifference) {
    // The property the whole design turns on. A child gets -0.12; when they turn 13 they should
    // end on -0.31, not on -0.43.
    auto scenario = create_intervention_scenario(three_bands("marketing"));
    auto random = source();
    auto person = person_aged(12);

    EXPECT_DOUBLE_EQ(25.0 - 0.12, scenario->apply(random, person, 2030, kBmi, 25.0));

    person.age = 13;
    const double moved = scenario->apply(random, person, 2031, kBmi, 25.0 - 0.12);
    EXPECT_DOUBLE_EQ(25.0 - 0.31, moved);
}

TEST(MarketingScenario, ClearForgetsWhoHasBeenExposed) {
    auto scenario = create_intervention_scenario(three_bands("marketing"));
    auto random = source();
    auto person = person_aged(30);

    EXPECT_DOUBLE_EQ(25.0 - 0.16, scenario->apply(random, person, 2030, kBmi, 25.0));
    scenario->clear();
    EXPECT_DOUBLE_EQ(25.0 - 0.16, scenario->apply(random, person, 2030, kBmi, 25.0));
}

// --- dynamic marketing --------------------------------------------------------------------------

TEST(DynamicMarketingScenario, AlphaOfOneAndBetaOfZeroIsPlainMarketing) {
    auto spec = three_bands("dynamic_marketing");
    spec.dynamics = {1.0, 0.0, 0.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();
    auto person = person_aged(30);

    EXPECT_DOUBLE_EQ(25.0 - 0.16, scenario->apply(random, person, 2030, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2031, kBmi, 25.0));
}

TEST(DynamicMarketingScenario, AlphaOfZeroMeansNobodyIsEverExposed) {
    auto spec = three_bands("dynamic_marketing");
    spec.dynamics = {0.0, 0.0, 0.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    for (int year = 2020; year <= 2040; ++year) {
        auto person = person_aged(30);
        EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, year, kBmi, 25.0));
    }
}

TEST(DynamicMarketingScenario, BetaOfOneTakesTheEffectBackOffTheNextYear) {
    auto spec = three_bands("dynamic_marketing");
    spec.dynamics = {1.0, 1.0, 0.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();
    auto person = person_aged(30);

    EXPECT_DOUBLE_EQ(25.0 - 0.16, scenario->apply(random, person, 2030, kBmi, 25.0));
    // Lapsed: the -0.16 they were carrying is added back.
    EXPECT_DOUBLE_EQ(25.0 + 0.16, scenario->apply(random, person, 2031, kBmi, 25.0));
}

TEST(DynamicMarketingScenario, TheParametersMustBeThreeProbabilities) {
    auto spec = three_bands("dynamic_marketing");
    spec.dynamics = {0.5, 0.1};
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);

    spec.dynamics = {0.5, 0.1, 1.5};
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);
}

TEST(DynamicMarketingScenario, OneDrawPerPersonPerYearWhateverHappens) {
    // The two scenarios of a trial run share a seed, so the number of draws must not depend on
    // which branch a person takes — otherwise the streams separate and the difference between the
    // futures is no longer attributable to the policy.
    auto spec = three_bands("dynamic_marketing");
    spec.dynamics = {0.5, 0.5, 0.5};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    const auto before = random.draw_count();
    for (std::size_t id = 1; id <= 50; ++id) {
        auto person = person_aged(30, id);
        scenario->apply(random, person, 2030, kBmi, 25.0);
    }
    const auto first_year = random.draw_count() - before;

    const auto middle = random.draw_count();
    for (std::size_t id = 1; id <= 50; ++id) {
        auto person = person_aged(31, id);
        scenario->apply(random, person, 2031, kBmi, 25.0);
    }
    EXPECT_EQ(first_year, random.draw_count() - middle);
}

// --- fiscal -------------------------------------------------------------------------------------

TEST(FiscalScenario, TheEffectIsAShareOfThePersonsOwnValue) {
    auto spec = three_bands("fiscal");
    spec.impact_type = "pessimist";
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    auto light = person_aged(30, 1);
    light.risk_factors[kBmi] = 20.0;
    EXPECT_DOUBLE_EQ(20.0 + 20.0 * -0.16, scenario->apply(random, light, 2030, kBmi, 20.0));

    auto heavy = person_aged(30, 2);
    heavy.risk_factors[kBmi] = 40.0;
    EXPECT_DOUBLE_EQ(40.0 + 40.0 * -0.16, scenario->apply(random, heavy, 2030, kBmi, 40.0));
}

TEST(FiscalScenario, ThePessimistAppliesTheAdultDifferenceAndTheOptimistDoesNot) {
    for (const auto *type : {"pessimist", "optimist"}) {
        auto spec = three_bands("fiscal");
        spec.impact_type = type;
        auto scenario = create_intervention_scenario(spec);
        auto random = source();

        auto person = person_aged(15);
        const double as_teenager = scenario->apply(random, person, 2030, kBmi, 25.0);
        EXPECT_DOUBLE_EQ(25.0 + 25.0 * -0.31, as_teenager) << type;

        person.age = 19;
        const double as_adult = scenario->apply(random, person, 2031, kBmi, 25.0);
        if (std::string{type} == "pessimist") {
            // The effect decays to the adult one: the difference is added back.
            EXPECT_DOUBLE_EQ(25.0 + 25.0 * (-0.16 - -0.31), as_adult);
        } else {
            // The habit sticks: nothing changes.
            EXPECT_DOUBLE_EQ(25.0, as_adult);
        }
    }
}

TEST(FiscalScenario, AnUnknownImpactTypeIsRejected) {
    auto spec = three_bands("fiscal");
    spec.impact_type = "realist";
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);
}

// --- physical activity ----------------------------------------------------------------------------

TEST(PhysicalActivityIntervention, FullCoverageAffectsEveryChild) {
    auto spec = three_bands("physical_activity");
    spec.impacts = {band(40.0, 6, 11), band(20.0, 12, std::nullopt)};
    spec.coverage_rates = {1.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    auto person = person_aged(8);
    EXPECT_DOUBLE_EQ(25.0 + 40.0, scenario->apply(random, person, 2030, kBmi, 25.0));
}

TEST(PhysicalActivityIntervention, NoCoverageAffectsNobody) {
    auto spec = three_bands("physical_activity");
    spec.impacts = {band(40.0, 6, 11), band(20.0, 12, std::nullopt)};
    spec.coverage_rates = {0.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    auto person = person_aged(8);
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2030, kBmi, 25.0));
}

TEST(PhysicalActivityIntervention, SomebodyFirstSeenAsAnAdultIsNeverAffected) {
    // The coverage draw happens once, in childhood: a school programme reaches somebody while
    // they are at school or not at all.
    auto spec = three_bands("physical_activity");
    spec.impacts = {band(40.0, 6, 11), band(20.0, 12, std::nullopt)};
    spec.coverage_rates = {1.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    auto person = person_aged(30);
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2030, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2031, kBmi, 25.0));
}

TEST(PhysicalActivityIntervention, AnAffectedChildMovesToTheAdultEffect) {
    auto spec = three_bands("physical_activity");
    spec.impacts = {band(40.0, 6, 11), band(20.0, 12, std::nullopt)};
    spec.coverage_rates = {1.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    auto person = person_aged(11);
    EXPECT_DOUBLE_EQ(25.0 + 40.0, scenario->apply(random, person, 2030, kBmi, 25.0));

    person.age = 12;
    EXPECT_DOUBLE_EQ(25.0 + 20.0 - 40.0, scenario->apply(random, person, 2031, kBmi, 25.0));
}

TEST(PhysicalActivityIntervention, ACoverageRateIsRequiredAndMustBeAProbability) {
    auto spec = three_bands("physical_activity");
    spec.impacts = {band(40.0, 6, 11), band(20.0, 12, std::nullopt)};
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);

    spec.coverage_rates = {1.5};
    EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError);
}

// --- food labelling -------------------------------------------------------------------------------

namespace {

InterventionSpec food_labelling_spec() {
    InterventionSpec spec;
    spec.identifier = "food_labelling";
    spec.active_period = hgps::config::PolicyPeriod{.start_time = 2020, .finish_time = 2050};
    spec.impacts = {band(-0.05, 5, std::nullopt)};
    spec.coverage_rates = {1.0, 1.0};
    spec.coverage_cutoff_time = 5;
    spec.child_cutoff_age = 18;
    // Child male, child female, adult male, adult female.
    spec.coefficients = {0.1, 0.2, 0.3, 0.4};
    spec.adjustments = {PolicyAdjustment{.risk_factor = "energy", .value = 0.5}};
    return spec;
}

} // namespace

TEST(FoodLabellingScenario, TheEffectIsTheProductOfItsFourTerms) {
    auto scenario = create_intervention_scenario(food_labelling_spec());
    auto random = source();

    auto person = person_aged(30);
    person.risk_factors[Identifier{"energy"}] = 2000.0;

    // transfer(adult male) * impact * the person's energy * the adjustment.
    EXPECT_DOUBLE_EQ(25.0 + 0.3 * -0.05 * 2000.0 * 0.5,
                     scenario->apply(random, person, 2030, kBmi, 25.0));
}

TEST(FoodLabellingScenario, TheTransferCoefficientDependsOnSexAndOnBeingAChild) {
    auto scenario = create_intervention_scenario(food_labelling_spec());
    auto random = source();

    const auto effect_for = [&](unsigned int age, Gender gender, std::size_t id) {
        Person person{gender, id};
        person.age = age;
        person.risk_factors[Identifier{"energy"}] = 2000.0;
        return scenario->apply(random, person, 2021, kBmi, 25.0) - 25.0;
    };

    const double base = -0.05 * 2000.0 * 0.5;
    EXPECT_DOUBLE_EQ(0.1 * base, effect_for(10, Gender::male, 1));
    EXPECT_DOUBLE_EQ(0.2 * base, effect_for(10, Gender::female, 2));
    EXPECT_DOUBLE_EQ(0.3 * base, effect_for(30, Gender::male, 3));
    EXPECT_DOUBLE_EQ(0.4 * base, effect_for(30, Gender::female, 4));
}

TEST(FoodLabellingScenario, APersonIsAffectedOnceAndNotAgain) {
    auto scenario = create_intervention_scenario(food_labelling_spec());
    auto random = source();

    auto person = person_aged(30);
    person.risk_factors[Identifier{"energy"}] = 2000.0;

    EXPECT_NE(25.0, scenario->apply(random, person, 2021, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2022, kBmi, 25.0));
}

TEST(FoodLabellingScenario, SomebodyMissedIsOfferedItAgainInsideTheWindowAndNotAfterIt) {
    auto spec = food_labelling_spec();
    spec.coverage_rates = {0.0, 1.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    auto person = person_aged(30);
    person.risk_factors[Identifier{"energy"}] = 2000.0;

    // Inside the five-year window, at a short-term rate of zero: nothing, every year.
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2021, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2022, kBmi, 25.0));

    // After the window nobody is reconsidered, whatever the long-term rate is: the question of
    // whether this person notices the label was settled while it was new.
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2026, kBmi, 25.0));
}

TEST(FoodLabellingScenario, SomebodyNotSeenUntilAfterTheWindowIsStillOfferedIt) {
    // A newborn, or an immigrant, first reaching the policy's age band after the window has
    // closed: the long-term rate is what decides for them.
    auto spec = food_labelling_spec();
    spec.coverage_rates = {0.0, 1.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    auto person = person_aged(30);
    person.risk_factors[Identifier{"energy"}] = 2000.0;

    EXPECT_NE(25.0, scenario->apply(random, person, 2026, kBmi, 25.0));
}

TEST(FoodLabellingScenario, APersonAffectedInTheWindowIsNotAffectedAgainInIt) {
    // Deviation B-24: the baseline marks them with try_emplace, which does nothing when they are
    // already marked unaffected, so it can apply the impact to the same person every remaining
    // year of the window.
    auto spec = food_labelling_spec();
    spec.coverage_rates = {1.0, 1.0};
    auto scenario = create_intervention_scenario(spec);
    auto random = source();

    auto person = person_aged(30);
    person.risk_factors[Identifier{"energy"}] = 2000.0;

    EXPECT_NE(25.0, scenario->apply(random, person, 2021, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2022, kBmi, 25.0));
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2023, kBmi, 25.0));
}

TEST(FoodLabellingScenario, APersonWithoutTheAdjustedFactorIsNotAffected) {
    auto scenario = create_intervention_scenario(food_labelling_spec());
    auto random = source();

    auto person = person_aged(30);
    EXPECT_DOUBLE_EQ(25.0, scenario->apply(random, person, 2021, kBmi, 25.0));
}

TEST(FoodLabellingScenario, TheParametersAreAllRequired) {
    for (const int missing : {0, 1, 2, 3}) {
        auto spec = food_labelling_spec();
        switch (missing) {
        case 0:
            spec.coverage_rates = {1.0};
            break;
        case 1:
            spec.coverage_cutoff_time.reset();
            break;
        case 2:
            spec.coefficients = {0.1, 0.2};
            break;
        default:
            spec.adjustments.clear();
            break;
        }
        EXPECT_THROW(create_intervention_scenario(spec), hgps::diag::InternalError)
            << "missing parameter " << missing;
    }
}
