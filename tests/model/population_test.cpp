// Ported from the baseline's src/HealthGPS.Tests/Population.Test.cpp (suite
// TestHealthGPS_Population, 34 tests), preserving each test's intent and values.
//
// Two adaptations, both deliberate:
//  - the baseline throws core::HgpsException for an unknown gender, sector, income, region or
//    ethnicity and std::logic_error for dying twice. All of those are programmer errors, so here
//    they are diag::InternalError (docs/decisions/0007-two-tier-diagnostics.md).
//  - the free-slot list means slot reuse is O(1) rather than a rescan (audit B-13), so the tests
//    that observe recycling check identity by person ID rather than by slot.
#include "model/population.h"
#include "model/two_step_value.h"

#include "diagnostics/internal_error.h"

#include <gtest/gtest.h>

#include <set>

using hgps::core::Gender;
using hgps::core::Identifier;
using hgps::core::Income;
using hgps::core::Sector;
using hgps::diag::InternalError;
using hgps::model::Disease;
using hgps::model::DiseaseStatus;
using hgps::model::Person;
using hgps::model::Population;
using hgps::model::TwoStepValue;

TEST(TestHealthGPS_Population, CreateDefaultPerson) {
    const Person person;

    EXPECT_EQ(Person::unassigned_id, person.id());
    EXPECT_FALSE(person.has_assigned_id());
    EXPECT_EQ(Gender::unknown, person.gender);
    EXPECT_EQ(0U, person.age);
    EXPECT_TRUE(person.is_alive());
    EXPECT_FALSE(person.has_emigrated());
    EXPECT_TRUE(person.is_active());
    EXPECT_TRUE(person.risk_factors.empty());
    EXPECT_TRUE(person.diseases.empty());
}

TEST(TestHealthGPS_Population, CreateUniquePerson) {
    const Population population{5};

    std::set<std::size_t> ids;
    for (const auto &person : population) {
        EXPECT_TRUE(person.has_assigned_id());
        EXPECT_TRUE(ids.insert(person.id()).second) << "duplicate id " << person.id();
    }

    EXPECT_EQ(5U, ids.size());
}

TEST(TestHealthGPS_Population, PersonStateIsActive) {
    const Person person{Gender::male, 1};

    EXPECT_TRUE(person.is_alive());
    EXPECT_FALSE(person.has_emigrated());
    EXPECT_TRUE(person.is_active());
    EXPECT_EQ(0U, person.time_of_death());
    EXPECT_EQ(0U, person.time_of_migration());
}

TEST(TestHealthGPS_Population, PersonStateDeath) {
    const unsigned int time_now = 2022;
    Person person{Gender::female, 1};

    person.die(time_now);

    EXPECT_FALSE(person.is_alive());
    EXPECT_FALSE(person.is_active());
    EXPECT_FALSE(person.has_emigrated());
    EXPECT_EQ(time_now, person.time_of_death());

    // Dying twice, or emigrating after death, is a broken invariant.
    EXPECT_THROW(person.die(time_now), InternalError);
    EXPECT_THROW(person.emigrate(time_now), InternalError);
}

TEST(TestHealthGPS_Population, PersonStateEmigrated) {
    const unsigned int time_now = 2022;
    Person person{Gender::female, 1};

    person.emigrate(time_now);

    EXPECT_TRUE(person.is_alive());
    EXPECT_TRUE(person.has_emigrated());
    EXPECT_FALSE(person.is_active());
    EXPECT_EQ(time_now, person.time_of_migration());

    EXPECT_THROW(person.die(time_now), InternalError);
    EXPECT_THROW(person.emigrate(time_now), InternalError);
}

TEST(TestHealthGPS_Population, CreateDefaultTwoStepValue) {
    const TwoStepValue<double> value;

    EXPECT_DOUBLE_EQ(0.0, value.value());
    EXPECT_DOUBLE_EQ(0.0, value.old_value());
    EXPECT_DOUBLE_EQ(0.0, value());
}

TEST(TestHealthGPS_Population, CreateCustomTwoStepValue) {
    const TwoStepValue<double> value{7.5};

    EXPECT_DOUBLE_EQ(7.5, value.value());
    EXPECT_DOUBLE_EQ(0.0, value.old_value());
}

TEST(TestHealthGPS_Population, AssignToTwoStepValue) {
    TwoStepValue<double> value{1.0};

    value = 2.0;
    EXPECT_DOUBLE_EQ(2.0, value.value());
    EXPECT_DOUBLE_EQ(1.0, value.old_value());

    value = 3.0;
    EXPECT_DOUBLE_EQ(3.0, value.value());
    EXPECT_DOUBLE_EQ(2.0, value.old_value());
}

TEST(TestHealthGPS_Population, SetBothTwoStepValues) {
    TwoStepValue<double> value{1.0};

    value.set_both_values(9.0);
    EXPECT_DOUBLE_EQ(9.0, value.value());
    EXPECT_DOUBLE_EQ(9.0, value.old_value());
}

TEST(TestHealthGPS_Population, CloneTwoStepValues) {
    TwoStepValue<double> value{1.0};
    value = 2.0;

    const auto clone = value.clone();
    EXPECT_DOUBLE_EQ(value.value(), clone.value());
    EXPECT_DOUBLE_EQ(value.old_value(), clone.old_value());
}

TEST(TestHealthGPS_Population, CreateDefaultDisease) {
    const Disease disease;

    EXPECT_EQ(DiseaseStatus::free, disease.status);
    EXPECT_EQ(0, disease.start_time);
    EXPECT_EQ(-1, disease.time_since_onset);
}

TEST(TestHealthGPS_Population, CloneDiseaseType) {
    const Disease disease{
        .status = DiseaseStatus::active, .start_time = 2015, .time_since_onset = 3};

    const auto clone = disease.clone();
    EXPECT_EQ(disease.status, clone.status);
    EXPECT_EQ(disease.start_time, clone.start_time);
    EXPECT_EQ(disease.time_since_onset, clone.time_since_onset);
}

TEST(TestHealthGPS_Population, AddSingleNewEntity) {
    Population population{3};
    const auto before = population.size();

    population.add(Person{Gender::male}, 2022);

    EXPECT_EQ(before + 1, population.size());
    EXPECT_EQ(before + 1, population.current_active_size());
    EXPECT_EQ(3U, population.initial_size());
    EXPECT_TRUE(population[before].has_assigned_id());
    EXPECT_EQ(Gender::male, population[before].gender);
}

TEST(TestHealthGPS_Population, AddMultipleNewEntities) {
    Population population{3};

    population.add_newborn_babies(4, Gender::female, 2022);

    EXPECT_EQ(7U, population.size());
    EXPECT_EQ(7U, population.current_active_size());
    for (std::size_t index = 3; index < population.size(); ++index) {
        EXPECT_EQ(Gender::female, population[index].gender);
        EXPECT_EQ(0U, population[index].age);
        EXPECT_TRUE(population[index].has_assigned_id());
    }
}

TEST(TestHealthGPS_Population, PersonIdInitialDeterministicAndPostInitialLifetimeUnique) {
    Population population{4};

    // The initial cohort is numbered 1..N by index, so person k is the same person in the
    // baseline and intervention scenarios (determinism clause D9).
    for (std::size_t index = 0; index < 4; ++index) {
        EXPECT_EQ(index + 1, population[index].id());
    }

    population.add(Person{Gender::male}, 2022);
    EXPECT_EQ(5U, population[4].id());

    population.add_newborn_babies(2, Gender::female, 2022);
    EXPECT_EQ(6U, population[5].id());
    EXPECT_EQ(7U, population[6].id());
}

TEST(TestHealthGPS_Population, PersonAddAssignsLifetimeUniqueId) {
    Population population{2};

    population.add(Person{Gender::male}, 2022);
    const auto first_new_id = population[2].id();

    population.add(Person{Gender::female}, 2022);
    const auto second_new_id = population[3].id();

    EXPECT_NE(first_new_id, second_new_id);
    EXPECT_GT(second_new_id, first_new_id);
}

TEST(TestHealthGPS_Population, AllPopulationIdsArePairwiseDistinct) {
    Population population{10};
    population.add_newborn_babies(5, Gender::male, 2022);
    population.add(Person{Gender::female}, 2022);

    std::set<std::size_t> ids;
    for (const auto &person : population) {
        EXPECT_TRUE(ids.insert(person.id()).second) << "duplicate id " << person.id();
    }

    EXPECT_EQ(population.size(), ids.size());
}

TEST(TestHealthGPS_Population, InitialCohortIdsAreConsecutiveFromOne) {
    const Population population{6};

    std::size_t expected = 1;
    for (const auto &person : population) {
        EXPECT_EQ(expected++, person.id());
    }
}

TEST(TestHealthGPS_Population, ANewEntrantIdIsNotOneAlreadyAssigned) {
    Population population{3};

    std::set<std::size_t> existing;
    for (const auto &person : population) {
        existing.insert(person.id());
    }

    population.add(Person{Gender::male}, 2022);
    population.add_newborn_babies(2, Gender::female, 2022);

    for (std::size_t index = 3; index < population.size(); ++index) {
        EXPECT_FALSE(existing.contains(population[index].id()));
    }
}

TEST(TestHealthGPS_Population, ARecycledSlotGetsAFreshIdNotTheOldOne) {
    // The property the earlier rewrite lost (audit R-03): a slot may be reused, an identifier may
    // not, or the per-person tracking output conflates two people.
    Population population{3};
    const auto dead_id = population[1].id();
    population.mark_died(1, 2022);

    population.add_newborn_babies(1, Gender::male, 2023);

    EXPECT_EQ(3U, population.size()) << "the free slot should have been reused";
    EXPECT_NE(dead_id, population[1].id());
    EXPECT_GT(population[1].id(), 3U);
    EXPECT_TRUE(population[1].is_active());
}

TEST(TestHealthGPS_Population, ASlotFreedThisYearIsNotReusedUntilTheNextYear) {
    // This year's deaths must still be visible to this year's analysis module.
    Population population{2};
    population.mark_died(0, 2022);

    population.add_newborn_babies(1, Gender::male, 2022);
    EXPECT_EQ(3U, population.size()) << "the slot was reused in the year the person died";

    population.add_newborn_babies(1, Gender::female, 2023);
    EXPECT_EQ(3U, population.size()) << "the slot should be reusable a year later";
}

TEST(TestHealthGPS_Population, ActiveSizeTracksDeathsAndEmigrations) {
    Population population{5};
    EXPECT_EQ(5U, population.current_active_size());

    population.mark_died(0, 2022);
    EXPECT_EQ(4U, population.current_active_size());

    population.mark_emigrated(1, 2022);
    EXPECT_EQ(3U, population.current_active_size());

    population.add_newborn_babies(2, Gender::male, 2023);
    // Two newborns, and the two freed slots are reused rather than appended.
    EXPECT_EQ(5U, population.current_active_size());
    EXPECT_EQ(5U, population.size());

    // Slot 0 now holds a live newborn, so it can die in turn — and marking a slot whose
    // occupant is already inactive is what throws.
    EXPECT_TRUE(population[0].is_active());
    population.mark_died(0, 2023);
    EXPECT_EQ(4U, population.current_active_size());
    EXPECT_THROW(population.mark_died(0, 2023), InternalError);
}

TEST(TestHealthGPS_Population, RefreshBookkeepingRederivesTheCounts) {
    Population population{4};

    // Mutating a person directly, as a model that touches Person::die would.
    population[2].die(2022);
    population.refresh_bookkeeping(2023);

    EXPECT_EQ(3U, population.current_active_size());
    population.add_newborn_babies(1, Gender::male, 2023);
    EXPECT_EQ(4U, population.size()) << "the freed slot should have been found by the refresh";
}

TEST(TestHealthGPS_Population, PersonIncomeValues) {
    Person person{Gender::male, 1};

    person.income = Income::low;
    EXPECT_FLOAT_EQ(1.0F, person.income_to_value());

    person.income = Income::middle;
    EXPECT_FLOAT_EQ(2.0F, person.income_to_value());

    person.income = Income::high;
    EXPECT_FLOAT_EQ(4.0F, person.income_to_value());

    person.income = Income::unknown;
    EXPECT_THROW(person.income_to_value(), InternalError);
}

TEST(TestHealthGPS_Population, PersonIncomeValuesLowerMiddle) {
    Person person{Gender::male, 1};
    person.income = Income::lowermiddle;
    // Both middle categories share a value, so a coefficient fitted on the three-category
    // encoding still means what it meant.
    EXPECT_FLOAT_EQ(2.0F, person.income_to_value());
}

TEST(TestHealthGPS_Population, PersonIncomeValuesUpperMiddle) {
    Person person{Gender::male, 1};
    person.income = Income::uppermiddle;
    EXPECT_FLOAT_EQ(3.0F, person.income_to_value());
}

TEST(TestHealthGPS_Population, PersonDefaultAgeAndGender) {
    const Person person;
    EXPECT_EQ(0U, person.age);
    EXPECT_EQ(Gender::unknown, person.gender);
    EXPECT_THROW(person.gender_to_value(), InternalError);
    EXPECT_THROW(person.gender_to_string(), InternalError);
}

TEST(TestHealthGPS_Population, PersonGenderValues) {
    Person person;

    person.gender = Gender::male;
    EXPECT_FLOAT_EQ(1.0F, person.gender_to_value());
    EXPECT_EQ("male", person.gender_to_string());

    person.gender = Gender::female;
    EXPECT_FLOAT_EQ(0.0F, person.gender_to_value());
    EXPECT_EQ("female", person.gender_to_string());
}

TEST(TestHealthGPS_Population, PersonDefaultIsActive) {
    const Person person;
    EXPECT_TRUE(person.is_active());
    EXPECT_TRUE(person.is_alive());
    EXPECT_FALSE(person.has_emigrated());
}

TEST(TestHealthGPS_Population, PersonSectorValues) {
    Person person;

    EXPECT_THROW(person.sector_to_value(), InternalError);

    person.sector = Sector::urban;
    EXPECT_FLOAT_EQ(0.0F, person.sector_to_value());

    person.sector = Sector::rural;
    EXPECT_FLOAT_EQ(1.0F, person.sector_to_value());
}

TEST(TestHealthGPS_Population, PersonRegionToValue) {
    Person person;

    person.region = "region1";
    EXPECT_FLOAT_EQ(1.0F, person.region_to_value());

    person.region = "region4";
    EXPECT_FLOAT_EQ(4.0F, person.region_to_value());

    person.region = "region12";
    EXPECT_FLOAT_EQ(12.0F, person.region_to_value());
}

TEST(TestHealthGPS_Population, PersonRegionToValueUnknownThrows) {
    Person person;
    EXPECT_THROW(person.region_to_value(), InternalError);

    person.region = "north";
    EXPECT_THROW(person.region_to_value(), InternalError);

    person.region = "regionX";
    EXPECT_THROW(person.region_to_value(), InternalError);
}

TEST(TestHealthGPS_Population, PersonEthnicityToValue) {
    Person person;

    person.ethnicity = "ethnicity1";
    EXPECT_FLOAT_EQ(1.0F, person.ethnicity_to_value());

    person.ethnicity = "ethnicity3";
    EXPECT_FLOAT_EQ(3.0F, person.ethnicity_to_value());
}

TEST(TestHealthGPS_Population, PersonEthnicityToValueUnknownThrows) {
    Person person;
    EXPECT_THROW(person.ethnicity_to_value(), InternalError);

    person.ethnicity = "white";
    EXPECT_THROW(person.ethnicity_to_value(), InternalError);
}

TEST(TestHealthGPS_Population, PersonGetRiskFactorValue) {
    Person person{Gender::male, 1};
    person.age = 40;
    person.ses = 0.5;
    person.risk_factors[Identifier{"bmi"}] = 24.5;

    EXPECT_DOUBLE_EQ(24.5, person.get_risk_factor_value(Identifier{"bmi"}));
    EXPECT_DOUBLE_EQ(40.0, person.get_risk_factor_value(Identifier{"age"}));
    EXPECT_DOUBLE_EQ(1600.0, person.get_risk_factor_value(Identifier{"age2"}));
    EXPECT_DOUBLE_EQ(64000.0, person.get_risk_factor_value(Identifier{"age3"}));
    EXPECT_DOUBLE_EQ(1.0, person.get_risk_factor_value(Identifier{"gender"}));
    EXPECT_DOUBLE_EQ(1.0, person.get_risk_factor_value(Identifier{"intercept"}));
    EXPECT_DOUBLE_EQ(0.5, person.get_risk_factor_value(Identifier{"ses"}));
    EXPECT_DOUBLE_EQ(1.0, person.get_risk_factor_value(Identifier{"over18"}));
}

TEST(TestHealthGPS_Population, PersonGetRiskFactorValueMissingThrows) {
    const Person person;
    // The baseline throws std::out_of_range; here it is an InternalError, because every
    // coefficient name is validated at model-load time, so an unknown one at run time is a bug.
    EXPECT_THROW(person.get_risk_factor_value(Identifier{"bmi"}), InternalError);
}

TEST(TestHealthGPS_Population, PersonDieMakesInactive) {
    Person person{Gender::male, 1};
    person.die(2020U);

    EXPECT_FALSE(person.is_active());
    EXPECT_FALSE(person.is_alive());
    EXPECT_EQ(2020U, person.time_of_death());
}

TEST(TestHealthGPS_Population, PersonEmigrateMakesInactive) {
    Person person{Gender::male, 1};
    person.emigrate(2020U);

    EXPECT_FALSE(person.is_active());
    EXPECT_TRUE(person.is_alive());
    EXPECT_EQ(2020U, person.time_of_migration());
}

TEST(TestHealthGPS_Population, PersonDieWhenNotActiveThrows) {
    Person person{Gender::male, 1};
    person.die(2020U);
    EXPECT_THROW(person.die(2021U), InternalError);
}

TEST(TestHealthGPS_Population, PersonEmigrateWhenNotActiveThrows) {
    Person person{Gender::male, 1};
    person.emigrate(2020U);
    EXPECT_THROW(person.emigrate(2021U), InternalError);
}

TEST(TestHealthGPS_Population, AtIsBoundsCheckedAndSubscriptIsNot) {
    Population population{2};

    EXPECT_NO_THROW(population.at(1));
    EXPECT_THROW(population.at(2), std::out_of_range);
    EXPECT_EQ(1U, population[0].id());
}

TEST(TestHealthGPS_Population, AddingManyPeopleIsNotQuadratic) {
    // Baseline finding B-13: find_index_of_recyclables rescans from index 0 on every call, once
    // per migrant per age per sex per year. This does not time anything — it checks the property
    // that makes the rescan unnecessary, that the free list is consumed rather than rediscovered.
    Population population{1000};

    for (std::size_t index = 0; index < 500; ++index) {
        population.mark_died(index, 2022);
    }

    population.add_newborn_babies(500, Gender::male, 2023);

    // Every death's slot was reused, so the population did not grow.
    EXPECT_EQ(1000U, population.size());
    EXPECT_EQ(1000U, population.current_active_size());

    std::set<std::size_t> ids;
    for (const auto &person : population) {
        EXPECT_TRUE(ids.insert(person.id()).second);
    }
    EXPECT_EQ(1000U, ids.size());
}
