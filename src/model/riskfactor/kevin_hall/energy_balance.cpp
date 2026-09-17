// The energy balance itself: foods to nutrients, nutrients to energy, and energy to a new body
// composition.
//
// The physiology is Hall's two-compartment model. Body fat and lean tissue move toward a steady
// state set by the balance between intake and expenditure, with glycogen and extracellular fluid
// following carbohydrate and sodium directly. Everything is per day, in kJ and kg.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: KevinHallModel::{compute_nutrient_intakes, compute_energy_intake,
//         initialise_kevin_hall_state, kevin_hall_run, adjust_weight, compute_G, compute_ECF,
//         compute_delta, compute_EE, compute_bmi} in src/HealthGPS/kevin_hall_model.cpp.
#include "kevin_hall_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <cmath>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kWeight{"weight"};
const core::Identifier kHeight{"height"};
const core::Identifier kBmi{"bmi"};
const core::Identifier kEnergyIntake{"energyintake"};
const core::Identifier kEnergyIntakePrevious{"energyintake_previous"};
const core::Identifier kPhysicalActivity{"physicalactivity"};
const core::Identifier kCarbohydrate{"carbohydrate"};
const core::Identifier kCarbohydratePrevious{"carbohydrate_previous"};
const core::Identifier kSodium{"sodium"};
const core::Identifier kSodiumPrevious{"sodium_previous"};
const core::Identifier kBodyFat{"bodyfat"};
const core::Identifier kLeanTissue{"leantissue"};
const core::Identifier kGlycogen{"glycogen"};
const core::Identifier kExtracellularFluid{"extracellularfluid"};
const core::Identifier kIntercept{"intercept_k"};
const core::Identifier kEnergyExpenditure{"energyexpenditure"};

double factor_of(const Person &person, const core::Identifier &key, std::string_view where) {
    const auto found = person.risk_factors.find(key);
    if (found == person.risk_factors.end()) {
        throw diag::InternalError(
            fmt::format("person {} has no '{}' in {}; the Kevin Hall model needs the static "
                        "model to have generated it first",
                        person.id(), key.to_string(), where));
    }
    return found->second;
}

} // namespace

double KevinHallModel::compute_glycogen(double carbohydrate, double carbohydrate_0,
                                         double glycogen_0) {
    // Glycogen scales with the square root of carbohydrate intake, calibrated so that the initial
    // intake reproduces the initial store.
    if (glycogen_0 <= 0.0 || carbohydrate_0 <= 0.0) {
        throw diag::InternalError(
            "the initial glycogen and carbohydrate intake must both be positive");
    }
    const double k = carbohydrate_0 / (glycogen_0 * glycogen_0);
    return std::sqrt(carbohydrate / k);
}

double KevinHallModel::compute_extracellular_fluid(double delta_sodium, double carbohydrate,
                                                    double carbohydrate_0, double fluid_0) {
    if (carbohydrate_0 <= 0.0) {
        throw diag::InternalError("the initial carbohydrate intake must be positive");
    }
    return fluid_0 +
           (delta_sodium - kXiCarbohydrate * (1.0 - carbohydrate / carbohydrate_0)) / kXiSodium;
}

double KevinHallModel::compute_delta(int age, core::Gender sex, double activity, double weight,
                                      double height) {
    if (weight <= 0.0) {
        throw diag::InternalError("body weight must be positive");
    }

    // Mifflin–St Jeor resting metabolic rate, in kcal, converted to kJ.
    double resting = 9.99 * weight + 6.25 * height - 4.92 * age;
    resting += sex == core::Gender::male ? 5.0 : -161.0;
    resting *= 4.184;

    // What is left of total expenditure once the resting rate and the thermic effect of food are
    // taken out, expressed per kg so it scales with the body it is spent on.
    return ((1.0 - kBetaTef) * activity - 1.0) * resting / weight;
}

double KevinHallModel::compute_expenditure(double weight, double fat, double lean, double intake,
                                            double intercept, double delta, double partition) {
    return (intercept + kGammaFat * fat + kGammaLean * lean + delta * weight + kBetaTef +
            kBetaAt + partition * intake) /
           (1.0 + partition);
}

void KevinHallModel::compute_nutrient_intakes(Person &person) const {
    for (const auto &[nutrient, unused] : parameters_->energy_equation) {
        person.risk_factors[nutrient] = 0.0;
    }

    for (const auto &[food, nutrients] : parameters_->nutrient_equations) {
        const double intake = factor_of(person, food, "the food-to-nutrient step");
        for (const auto &[nutrient, coefficient] : nutrients) {
            person.risk_factors.at(nutrient) += intake * coefficient;
        }
    }
}

void KevinHallModel::initialise_nutrient_intakes(Person &person) const {
    compute_nutrient_intakes(person);

    // The balance works on year-on-year *changes*, so the first year's "previous" is this year's.
    person.risk_factors[kCarbohydratePrevious] =
        factor_of(person, kCarbohydrate, "initialising nutrient intakes");
    person.risk_factors[kSodiumPrevious] =
        factor_of(person, kSodium, "initialising nutrient intakes");
}

void KevinHallModel::update_nutrient_intakes(Person &person) const {
    person.risk_factors[kCarbohydratePrevious] =
        factor_of(person, kCarbohydrate, "updating nutrient intakes");
    person.risk_factors[kSodiumPrevious] = factor_of(person, kSodium, "updating nutrient intakes");
    compute_nutrient_intakes(person);
}

void KevinHallModel::compute_energy_intake(Person &person) const {
    double energy = 0.0;
    for (const auto &[nutrient, coefficient] : parameters_->energy_equation) {
        energy += factor_of(person, nutrient, "the nutrient-to-energy step") * coefficient;
    }
    person.risk_factors[kEnergyIntake] = energy;
}

void KevinHallModel::initialise_energy_intake(Person &person) const {
    compute_energy_intake(person);
    person.risk_factors[kEnergyIntakePrevious] = person.risk_factors.at(kEnergyIntake);
}

void KevinHallModel::update_energy_intake(Person &person) const {
    person.risk_factors[kEnergyIntakePrevious] =
        factor_of(person, kEnergyIntake, "updating energy intake");
    compute_energy_intake(person);
}

void KevinHallModel::compute_bmi(Person &person) const {
    const double weight = factor_of(person, kWeight, "computing BMI");
    const double height = factor_of(person, kHeight, "computing BMI") / 100.0;
    if (height <= 0.0) {
        throw diag::InternalError(
            fmt::format("person {} has a height of {} cm, so their BMI is undefined", person.id(),
                        height * 100.0));
    }
    person.risk_factors[kBmi] = weight / (height * height);
}

void KevinHallModel::initialise_state(RuntimeContext &context, Person &person,
                                       std::optional<double> adjustment) const {
    if (adjustment.has_value()) {
        person.risk_factors.at(kWeight) += *adjustment;
        validate_weight(context, person, "state initialisation");
    }

    const double height = factor_of(person, kHeight, "initialising the energy balance state");
    const double weight = factor_of(person, kWeight, "initialising the energy balance state");
    const double activity =
        factor_of(person, kPhysicalActivity, "initialising the energy balance state");
    const double intake = factor_of(person, kEnergyIntake, "initialising the energy balance state");

    if (height <= 0.0) {
        throw diag::InternalError(fmt::format(
            "person {} has a height of {} cm, so their body fat cannot be estimated", person.id(),
            height));
    }

    // Deurenberg's body-fat equation, one set of coefficients per sex.
    const double bmi = weight / std::pow(height / 100.0, 2.0);
    double fat = person.gender == core::Gender::male
                     ? weight * (0.14 * person.age + 37.31 * std::log(bmi) - 103.94) / 100.0
                     : weight * (0.14 * person.age + 39.96 * std::log(bmi) - 102.01) / 100.0;

    if (fat < 0.0) {
        // The equation can go negative for a very light body. Twenty per cent is the floor the
        // baseline uses, and it is a floor rather than a model: it keeps the partition coefficient
        // below finite, which a zero or negative fat mass would not.
        fat = 0.2 * weight;
    }

    const double glycogen = 0.01 * weight;
    const double water = 2.7 * glycogen;
    const double fluid = 0.7 * 0.235 * weight;
    const double lean = weight - fat - glycogen - water - fluid;

    const double delta = compute_delta(static_cast<int>(person.age), person.gender, activity,
                                        weight, height);

    // The intercept absorbs whatever the equation does not explain, so that this person starts in
    // balance: their expenditure equals their intake in the first year by construction.
    const double intercept = intake - (kGammaFat * fat + kGammaLean * lean + delta * weight);

    const double c = 10.4 * kRhoLean / kRhoFat;
    const double p = c / (c + fat);
    const double partition = p * kEtaLean / kRhoLean + (1.0 - p) * kEtaFat / kRhoFat;

    person.risk_factors[kBodyFat] = fat;
    person.risk_factors[kLeanTissue] = lean;
    person.risk_factors[kGlycogen] = glycogen;
    person.risk_factors[kExtracellularFluid] = fluid;
    person.risk_factors[kIntercept] = intercept;
    person.risk_factors[kEnergyExpenditure] =
        compute_expenditure(weight, fat, lean, intake, intercept, delta, partition);
}

void KevinHallModel::run_energy_balance(RuntimeContext &context, Person &person) const {
    const double weight_0 = factor_of(person, kWeight, "the energy balance");
    const double activity = factor_of(person, kPhysicalActivity, "the energy balance");
    const double height = factor_of(person, kHeight, "the energy balance");
    const double delta = compute_delta(static_cast<int>(person.age), person.gender, activity,
                                        weight_0, height);

    const double carbohydrate_0 = factor_of(person, kCarbohydratePrevious, "the energy balance");
    const double carbohydrate = factor_of(person, kCarbohydrate, "the energy balance");
    const double intake_0 = factor_of(person, kEnergyIntakePrevious, "the energy balance");
    const double intake = factor_of(person, kEnergyIntake, "the energy balance");
    const double delta_intake = intake - intake_0;

    // Eating more costs energy to digest and raises the metabolic rate a little; both scale with
    // the change rather than the level.
    const double thermic = kBetaTef * delta_intake;
    const double thermogenesis = kBetaAt * delta_intake;

    const double glycogen_0 = factor_of(person, kGlycogen, "the energy balance");
    const double glycogen = compute_glycogen(carbohydrate, carbohydrate_0, glycogen_0);
    const double water = 2.7 * glycogen;

    const double sodium_0 = factor_of(person, kSodiumPrevious, "the energy balance");
    const double sodium = factor_of(person, kSodium, "the energy balance");
    const double fluid_0 = factor_of(person, kExtracellularFluid, "the energy balance");
    const double fluid = compute_extracellular_fluid(sodium - sodium_0, carbohydrate,
                                                      carbohydrate_0, fluid_0);

    const double fat_0 = factor_of(person, kBodyFat, "the energy balance");
    const double lean_0 = factor_of(person, kLeanTissue, "the energy balance");
    const double intercept = factor_of(person, kIntercept, "the energy balance");

    // How an energy surplus is split between fat and lean tissue. The split depends on how much
    // fat there already is: the more there is, the more of the surplus goes to fat.
    const double c = 10.4 * kRhoLean / kRhoFat;
    const double p = c / (c + fat_0);
    const double partition = p * kEtaLean / kRhoLean + (1.0 - p) * kEtaFat / kRhoFat;

    // Two linear equations in (fat, lean): the first fixes the partition, the second the balance
    // between intake and expenditure. Their solution is the steady state this year moves toward.
    const double a1 = p * kRhoFat;
    const double b1 = -(1.0 - p) * kRhoLean;
    const double c1 = p * kRhoFat * fat_0 - (1.0 - p) * kRhoLean * lean_0;

    const double a2 = kGammaFat + delta;
    const double b2 = kGammaLean + delta;
    const double c2 = intake - intercept - thermic - thermogenesis - delta * (glycogen + water +
                                                                              fluid);

    const double determinant = a1 * b2 - a2 * b1;
    if (determinant == 0.0) {
        throw diag::InternalError(
            fmt::format("person {}'s energy balance has no steady state: the fat and lean "
                        "equations are linearly dependent",
                        person.id()));
    }

    const double steady_fat = -(b1 * c2 - b2 * c1) / determinant;
    const double steady_lean = -(c1 * a2 - c2 * a1) / determinant;

    // The time constant of the approach to that steady state, and one year of it.
    const double tau = kRhoLean * kRhoFat * (1.0 + partition) /
                       ((kGammaFat + delta) * (1.0 - p) * kRhoLean +
                        (kGammaLean + delta) * p * kRhoFat);
    const double reached = std::exp(-365.0 / tau);

    const double fat = steady_fat - (steady_fat - fat_0) * reached;
    const double lean = steady_lean - (steady_lean - lean_0) * reached;

    person.risk_factors.at(kGlycogen) = glycogen;
    person.risk_factors.at(kExtracellularFluid) = fluid;
    person.risk_factors.at(kBodyFat) = fat;
    person.risk_factors.at(kLeanTissue) = lean;
    person.risk_factors.at(kWeight) = fat + lean + glycogen + water + fluid;

    validate_weight(context, person, "the energy balance");
}

void KevinHallModel::adjust_weight(RuntimeContext &context, Person &person,
                                    double adjustment) const {
    const double height = factor_of(person, kHeight, "the weight adjustment");
    const double weight_0 = factor_of(person, kWeight, "the weight adjustment");
    const double fat_0 = factor_of(person, kBodyFat, "the weight adjustment");
    const double lean_0 = factor_of(person, kLeanTissue, "the weight adjustment");
    const double glycogen = factor_of(person, kGlycogen, "the weight adjustment");
    const double water = 2.7 * glycogen;
    const double fluid_0 = factor_of(person, kExtracellularFluid, "the weight adjustment");
    const double activity = factor_of(person, kPhysicalActivity, "the weight adjustment");
    const double intake = factor_of(person, kEnergyIntake, "the weight adjustment");
    const double intercept_0 = factor_of(person, kIntercept, "the weight adjustment");

    const double c = 10.4 * kRhoLean / kRhoFat;

    const double p_0 = c / (c + fat_0);
    const double partition_0 = p_0 * kEtaLean / kRhoLean + (1.0 - p_0) * kEtaFat / kRhoFat;
    const double delta_0 = compute_delta(static_cast<int>(person.age), person.gender, activity,
                                          weight_0, height);
    const double expenditure_0 = compute_expenditure(weight_0, fat_0, lean_0, intake, intercept_0,
                                                      delta_0, partition_0);

    const double weight = weight_0 + adjustment;

    // Glycogen and its bound water are set by carbohydrate intake, not by the calibration, so the
    // adjustment is taken out of the rest of the body in proportion.
    const double divisor = weight_0 - glycogen - water;
    if (divisor == 0.0) {
        throw diag::InternalError(
            fmt::format("person {} has no body mass outside glycogen and its water, so the "
                        "weight adjustment cannot be apportioned",
                        person.id()));
    }
    const double ratio = (weight - glycogen - water) / divisor;

    const double fat = fat_0 * ratio;
    const double lean = lean_0 * ratio;
    const double fluid = fluid_0 * ratio;

    const double p = c / (c + fat);
    const double partition = p * kEtaLean / kRhoLean + (1.0 - p) * kEtaFat / kRhoFat;
    const double delta = compute_delta(static_cast<int>(person.age), person.gender, activity,
                                        weight, height);
    const double expenditure =
        compute_expenditure(weight, fat, lean, intake, intercept_0, delta, partition);

    // The intercept moves so the calibrated body is still in the same balance the uncalibrated one
    // was: the adjustment must not create or destroy energy.
    person.risk_factors.at(kWeight) = weight;
    person.risk_factors.at(kBodyFat) = fat;
    person.risk_factors.at(kLeanTissue) = lean;
    person.risk_factors.at(kExtracellularFluid) = fluid;
    person.risk_factors.at(kIntercept) =
        intercept_0 + (1.0 + partition) * (expenditure_0 - expenditure);
    person.risk_factors[kEnergyExpenditure] = expenditure;

    validate_weight(context, person, "the weight adjustment");
}

} // namespace hgps::model
