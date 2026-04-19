#include "hgps_core/diagnostics/internal_error.h"

#include "kevin_hall_model.h"
#include "events/sync_message.h"
#include "simulation/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

using hgps::core::operator""_id;

namespace {

using KevinHallAdjustmentMessage =
    hgps::SyncDataMessage<hgps::UnorderedMap2d<hgps::core::Gender, int, double>>;

} // namespace

// NOLINTBEGIN(readability-convert-member-functions-to-static)
namespace hgps {

KevinHallModel::KevinHallModel(
    std::shared_ptr<RiskFactorSexAgeTable> expected,
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
    std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
    const std::unordered_map<core::Identifier, double> &energy_equation,
    const std::unordered_map<core::Identifier, core::DoubleInterval> &nutrient_ranges,
    const std::unordered_map<core::Identifier, std::map<core::Identifier, double>>
        &nutrient_equations,
    const std::unordered_map<core::Identifier, std::optional<double>> &food_prices,
    const std::unordered_map<core::Gender, std::vector<double>> &weight_quantiles,
    const std::vector<double> &epa_quantiles,
    const std::unordered_map<core::Gender, double> &height_stddev,
    const std::unordered_map<core::Gender, double> &height_slope)
    : RiskFactorAdjustableModel{std::move(expected), std::move(expected_trend),
                                std::move(trend_steps)},
      energy_equation_{energy_equation}, nutrient_ranges_{nutrient_ranges},
      nutrient_equations_{nutrient_equations}, food_prices_{food_prices},
      weight_quantiles_{weight_quantiles}, epa_quantiles_{epa_quantiles},
      height_stddev_{height_stddev}, height_slope_{height_slope} {
    if (energy_equation_.empty()) {
        throw core::InternalError("Energy equation mapping is empty");
    }
    if (nutrient_ranges_.empty()) {
        throw core::InternalError("Nutrient ranges mapping is empty");
    }
    if (nutrient_equations_.empty()) {
        throw core::InternalError("Nutrient equation mapping is empty");
    }
    if (food_prices_.empty()) {
        throw core::InternalError("Food prices mapping is empty");
    }
    if (weight_quantiles_.empty()) {
        throw core::InternalError("Weight quantiles mapping is empty");
    }
    if (epa_quantiles_.empty()) {
        throw core::InternalError("Energy Physical Activity quantiles mapping is empty");
    }
    if (height_stddev_.empty()) {
        throw core::InternalError("Height standard deviation mapping is empty");
    }
    if (height_slope_.empty()) {
        throw core::InternalError("Height slope mapping is empty");
    }
}

RiskFactorModelType KevinHallModel::type() const noexcept { return RiskFactorModelType::Dynamic; }

std::string KevinHallModel::name() const { return "Dynamic"; }

void KevinHallModel::generate_risk_factors(RuntimeContext &context) {
    for (auto &person : context.population()) {
        initialise_nutrient_intakes(person);
        initialise_energy_intake(person);
        initialise_weight(context, person);
    }

    adjust_risk_factors(context, {"Weight"_id}, std::nullopt, true);

    auto W_power_means = compute_mean_weight(context.population(), height_slope_);

    for (auto &person : context.population()) {
        const double W_power_mean = W_power_means.at(person.gender, person.age);
        initialise_height(context, person, W_power_mean, context.random());
        initialise_kevin_hall_state(person);
        compute_bmi(person);
    }
}

void KevinHallModel::update_risk_factors(RuntimeContext &context) {
    update_newborns(context);
    update_non_newborns(context);

    for (auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }

        compute_bmi(person);
    }
}

void KevinHallModel::update_newborns(RuntimeContext &context) const {
    for (auto &person : context.population()) {
        if (!person.is_active() || person.age != 0) {
            continue;
        }

        initialise_nutrient_intakes(person);
        initialise_energy_intake(person);
        initialise_weight(context, person);
    }

    auto adjustments = compute_weight_adjustments(context, 0);
    for (auto &person : context.population()) {
        if (!person.is_active() || person.age != 0) {
            continue;
        }

        const double adjustment = adjustments.at(person.gender, person.age);
        person.risk_factors.at("Weight"_id) += adjustment;
    }

    auto W_power_means = compute_mean_weight(context.population(), height_slope_, 0);

    for (auto &person : context.population()) {
        if (!person.is_active() || person.age != 0) {
            continue;
        }

        const double W_power_mean = W_power_means.at(person.gender, person.age);
        initialise_height(context, person, W_power_mean, context.random());
        initialise_kevin_hall_state(person);
    }
}

void KevinHallModel::update_non_newborns(RuntimeContext &context) const {
    for (auto &person : context.population()) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        update_nutrient_intakes(person);
        update_energy_intake(person);
    }

    for (auto &person : context.population()) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        if (person.age < kevin_hall_age_min) {
            initialise_weight(context, person);
        } else {
            kevin_hall_run(person);
        }
    }

    auto adjustments = receive_weight_adjustments(context);

    for (auto &person : context.population()) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        double adjustment = 0.0;
        if (adjustments.contains(person.gender, person.age)) {
            adjustment = adjustments.at(person.gender, person.age);
        }

        if (person.age < kevin_hall_age_min) {
            initialise_kevin_hall_state(person, adjustment);
        } else {
            adjust_weight(person, adjustment);
        }
    }

    send_weight_adjustments(context, std::move(adjustments));

    auto W_power_means = compute_mean_weight(context.population(), height_slope_);

    for (auto &person : context.population()) {
        if (!person.is_active() || person.age == 0 || person.age >= kevin_hall_age_min) {
            continue;
        }

        const double W_power_mean = W_power_means.at(person.gender, person.age);
        update_height(context, person, W_power_mean);
    }
}

KevinHallAdjustmentTable KevinHallModel::receive_weight_adjustments(RuntimeContext &context) const {
    KevinHallAdjustmentTable adjustments;

    if (context.scenario().type() == ScenarioType::baseline) {
        return compute_weight_adjustments(context);
    }

    auto message = context.sync_channel().try_receive(context.sync_timeout_millis());
    if (!message.has_value()) {
        throw core::InternalError(
            "Simulation out of sync, receive Kevin Hall adjustments message has timed out");
    }

    auto &basePtr = message.value();
    auto *messagePrt = dynamic_cast<KevinHallAdjustmentMessage *>(basePtr.get());
    if (!messagePrt) {
        throw core::InternalError(
            "Simulation out of sync, failed to receive a Kevin Hall adjustments message");
    }

    return messagePrt->data();
}

void KevinHallModel::send_weight_adjustments(RuntimeContext &context,
                                             KevinHallAdjustmentTable &&adjustments) const {
    if (context.scenario().type() == ScenarioType::baseline) {
        context.sync_channel().send(std::make_unique<KevinHallAdjustmentMessage>(
            context.current_run(), context.time_now(), std::move(adjustments)));
    }
}

KevinHallAdjustmentTable
KevinHallModel::compute_weight_adjustments(RuntimeContext &context,
                                           std::optional<unsigned> age) const {
    auto W_means = compute_mean_weight(context.population(), std::nullopt, age);

    auto adjustments = KevinHallAdjustmentTable{};
    for (const auto &[W_mean_sex, W_means_by_sex] : W_means) {
        adjustments.emplace_row(W_mean_sex, std::unordered_map<int, double>{});
        for (const auto &[W_mean_age, W_mean] : W_means_by_sex) {
            const double W_expected =
                get_expected(context, W_mean_sex, W_mean_age, "Weight"_id, std::nullopt, true);
            adjustments.at(W_mean_sex)[W_mean_age] = W_expected - W_mean;
        }
    }

    return adjustments;
}

double KevinHallModel::get_expected(RuntimeContext &context, core::Gender sex, int age,
                                    const core::Identifier &factor, OptionalRange range,
                                    bool apply_trend) const {
    if (energy_equation_.contains(factor)) {
        double nutrient_intake = 0.0;
        for (const auto &[food_key, nutrient_coefficients] : nutrient_equations_) {
            const double food_intake =
                get_expected(context, sex, age, food_key, std::nullopt, apply_trend);
            if (nutrient_coefficients.contains(factor)) {
                nutrient_intake += food_intake * nutrient_coefficients.at(factor);
            }
        }
        return nutrient_intake;
    }

    if (factor == "EnergyIntake"_id) {
        if (!apply_trend) {
            return RiskFactorAdjustableModel::get_expected(context, sex, age, "EnergyIntake"_id,
                                                           std::nullopt, false);
        }

        double energy_intake = 0.0;
        for (const auto &[nutrient_key, energy_coefficient] : energy_equation_) {
            const double nutrient_intake =
                get_expected(context, sex, age, nutrient_key, std::nullopt, true);
            energy_intake += nutrient_intake * energy_coefficient;
        }
        return energy_intake;
    }

    if (factor == "Weight"_id) {
        if (!apply_trend) {
            return RiskFactorAdjustableModel::get_expected(context, sex, age, "Weight"_id,
                                                           std::nullopt, false);
        }

        if (age < kevin_hall_age_min) {
            double weight = RiskFactorAdjustableModel::get_expected(context, sex, age, "Weight"_id,
                                                                    std::nullopt, false);
            const double energy_without_trend =
                get_expected(context, sex, age, "EnergyIntake"_id, std::nullopt, false);
            const double energy_with_trend =
                get_expected(context, sex, age, "EnergyIntake"_id, std::nullopt, true);

            if (energy_without_trend > 0.0) {
                weight *= std::pow(energy_with_trend / energy_without_trend, 0.45);
            }

            return weight;
        }

        double weight = 16.1161;

        const double pa_from_factors_mean =
            get_expected(context, sex, age, "PhysicalActivity"_id, std::nullopt, false);
        const double pal_coefficient =
            pa_from_factors_mean > 0.0 ? 1.0 / (9.99 * pa_from_factors_mean) : 0.0;

        weight += pal_coefficient *
                  get_expected(context, sex, age, "EnergyIntake"_id, std::nullopt, true);
        weight -= 0.6256 * get_expected(context, sex, age, "Height"_id, std::nullopt, false);
        weight += 0.4925 * age;

        const double gender_coefficient = (sex == core::Gender::male) ? 16.6166 : 0.0;
        weight -= gender_coefficient;

        return weight;
    }

    return RiskFactorAdjustableModel::get_expected(context, sex, age, factor, range, apply_trend);
}

void KevinHallModel::initialise_nutrient_intakes(Person &person) const {
    compute_nutrient_intakes(person);

    if (!person.risk_factors.contains("Carbohydrate"_id)) {
        throw core::InternalError("Missing 'Carbohydrate' in initialise_nutrient_intakes");
    }
    if (!person.risk_factors.contains("Sodium"_id)) {
        throw core::InternalError("Missing 'Sodium' in initialise_nutrient_intakes");
    }

    const double carbohydrate = person.risk_factors.at("Carbohydrate"_id);
    person.risk_factors["Carbohydrate_previous"_id] = carbohydrate;

    const double sodium = person.risk_factors.at("Sodium"_id);
    person.risk_factors["Sodium_previous"_id] = sodium;
}

void KevinHallModel::update_nutrient_intakes(Person &person) const {
    const double previous_carbohydrate = person.risk_factors.at("Carbohydrate"_id);
    person.risk_factors.at("Carbohydrate_previous"_id) = previous_carbohydrate;

    const double previous_sodium = person.risk_factors.at("Sodium"_id);
    person.risk_factors.at("Sodium_previous"_id) = previous_sodium;

    compute_nutrient_intakes(person);
}

void KevinHallModel::compute_nutrient_intakes(Person &person) const {
    for (const auto &[nutrient_key, _] : energy_equation_) {
        person.risk_factors[nutrient_key] = 0.0;
    }

    for (const auto &[food_key, nutrient_coefficients] : nutrient_equations_) {
        if (!person.risk_factors.contains(food_key)) {
            throw core::InternalError("Missing food risk factor in Kevin Hall model");
        }

        const double food_intake = person.risk_factors.at(food_key);
        for (const auto &[nutrient_key, nutrient_coefficient] : nutrient_coefficients) {
            person.risk_factors.at(nutrient_key) += food_intake * nutrient_coefficient;
        }
    }
}

void KevinHallModel::initialise_energy_intake(Person &person) const {
    compute_energy_intake(person);

    if (!person.risk_factors.contains("EnergyIntake"_id)) {
        throw core::InternalError("Missing 'EnergyIntake' in initialise_energy_intake");
    }

    const double energy_intake = person.risk_factors.at("EnergyIntake"_id);
    person.risk_factors["EnergyIntake_previous"_id] = energy_intake;
}

void KevinHallModel::update_energy_intake(Person &person) const {
    const double previous_energy_intake = person.risk_factors.at("EnergyIntake"_id);
    person.risk_factors.at("EnergyIntake_previous"_id) = previous_energy_intake;

    compute_energy_intake(person);
}

void KevinHallModel::compute_energy_intake(Person &person) const {
    const auto energy_intake_key = "EnergyIntake"_id;
    person.risk_factors[energy_intake_key] = 0.0;

    for (const auto &[nutrient_key, energy_coefficient] : energy_equation_) {
        if (!person.risk_factors.contains(nutrient_key)) {
            throw core::InternalError("Missing nutrient key in compute_energy_intake");
        }

        const double nutrient_intake = person.risk_factors.at(nutrient_key);
        person.risk_factors.at(energy_intake_key) += nutrient_intake * energy_coefficient;
    }
}

void KevinHallModel::initialise_kevin_hall_state(Person &person,
                                                 std::optional<double> adjustment) const {
    if (adjustment.has_value()) {
        person.risk_factors.at("Weight"_id) += adjustment.value();
    }

    if (!person.risk_factors.contains("Height"_id)) {
        throw core::InternalError("Missing 'Height' in initialise_kevin_hall_state");
    }
    if (!person.risk_factors.contains("Weight"_id)) {
        throw core::InternalError("Missing 'Weight' in initialise_kevin_hall_state");
    }
    if (!person.risk_factors.contains("PhysicalActivity"_id)) {
        throw core::InternalError("Missing 'PhysicalActivity' in initialise_kevin_hall_state");
    }
    if (!person.risk_factors.contains("EnergyIntake"_id)) {
        throw core::InternalError("Missing 'EnergyIntake' in initialise_kevin_hall_state");
    }

    const double H = person.risk_factors.at("Height"_id);
    const double BW = person.risk_factors.at("Weight"_id);
    const double PAL = person.risk_factors.at("PhysicalActivity"_id);
    const double EI = person.risk_factors.at("EnergyIntake"_id);

    double F;
    if (person.gender == core::Gender::female) {
        F = BW * (0.14 * person.age + 39.96 * std::log(BW / std::pow(H / 100.0, 2.0)) - 102.01) /
            100.0;
    } else if (person.gender == core::Gender::male) {
        F = BW * (0.14 * person.age + 37.31 * std::log(BW / std::pow(H / 100.0, 2.0)) - 103.94) /
            100.0;
    } else {
        throw core::InternalError("Unknown gender");
    }

    if (F < 0.0) {
        F = 0.2 * BW;
    }

    const double G = 0.01 * BW;
    const double W = 2.7 * G;
    const double ECF = 0.7 * 0.235 * BW;
    const double L = BW - F - G - W - ECF;

    const double delta = compute_delta(person.age, person.gender, PAL, BW, H);
    const double K = EI - (gamma_F * F + gamma_L * L + delta * BW);

    const double C = 10.4 * rho_L / rho_F;
    const double p = C / (C + F);
    const double x = p * eta_L / rho_L + (1.0 - p) * eta_F / rho_F;
    const double EE = compute_EE(BW, F, L, EI, K, delta, x);

    person.risk_factors["BodyFat"_id] = F;
    person.risk_factors["LeanTissue"_id] = L;
    person.risk_factors["Glycogen"_id] = G;
    person.risk_factors["ExtracellularFluid"_id] = ECF;
    person.risk_factors["Intercept_K"_id] = K;
    person.risk_factors["EnergyExpenditure"_id] = EE;
}

void KevinHallModel::kevin_hall_run(Person &person) const {
    const double BW_0 = person.risk_factors.at("Weight"_id);

    const double PAL = person.risk_factors.at("PhysicalActivity"_id);
    const double H = person.risk_factors.at("Height"_id);
    const double delta = compute_delta(person.age, person.gender, PAL, BW_0, H);

    const double CI_0 = person.risk_factors.at("Carbohydrate_previous"_id);
    const double CI = person.risk_factors.at("Carbohydrate"_id);
    const double EI_0 = person.risk_factors.at("EnergyIntake_previous"_id);
    const double EI = person.risk_factors.at("EnergyIntake"_id);
    const double delta_EI = EI - EI_0;

    const double TEF = beta_TEF * delta_EI;
    const double AT = beta_AT * delta_EI;

    const double G_0 = person.risk_factors.at("Glycogen"_id);
    const double G = compute_G(CI, CI_0, G_0);
    const double W = 2.7 * G;

    const double Na_0 = person.risk_factors.at("Sodium_previous"_id);
    const double Na = person.risk_factors.at("Sodium"_id);
    const double delta_Na = Na - Na_0;
    const double ECF_0 = person.risk_factors.at("ExtracellularFluid"_id);
    const double ECF = compute_ECF(delta_Na, CI, CI_0, ECF_0);

    const double F_0 = person.risk_factors.at("BodyFat"_id);
    const double L_0 = person.risk_factors.at("LeanTissue"_id);
    const double K = person.risk_factors.at("Intercept_K"_id);

    const double C = 10.4 * rho_L / rho_F;
    const double p = C / (C + F_0);
    const double x = p * eta_L / rho_L + (1.0 - p) * eta_F / rho_F;

    const double a1 = p * rho_F;
    const double b1 = -(1.0 - p) * rho_L;
    const double c1 = p * rho_F * F_0 - (1.0 - p) * rho_L * L_0;

    const double a2 = gamma_F + delta;
    const double b2 = gamma_L + delta;
    const double c2 = EI - K - TEF - AT - delta * (G + W + ECF);

    const double denominator = a1 * b2 - a2 * b1;
    if (denominator == 0.0) {
        throw core::InternalError("Kevin Hall model equilibrium denominator is zero");
    }

    const double steady_F = -(b1 * c2 - b2 * c1) / denominator;
    const double steady_L = -(c1 * a2 - c2 * a1) / denominator;

    const double tau_denominator =
        (gamma_F + delta) * (1.0 - p) * rho_L + (gamma_L + delta) * p * rho_F;
    if (tau_denominator == 0.0) {
        throw core::InternalError("Kevin Hall model tau denominator is zero");
    }

    const double tau = rho_L * rho_F * (1.0 + x) / tau_denominator;

    const double F = steady_F - (steady_F - F_0) * std::exp(-365.0 / tau);
    const double L = steady_L - (steady_L - L_0) * std::exp(-365.0 / tau);
    const double BW = F + L + G + W + ECF;

    person.risk_factors.at("Glycogen"_id) = G;
    person.risk_factors.at("ExtracellularFluid"_id) = ECF;
    person.risk_factors.at("BodyFat"_id) = F;
    person.risk_factors.at("LeanTissue"_id) = L;
    person.risk_factors.at("Weight"_id) = BW;
}

double KevinHallModel::compute_G(double CI, double CI_0, double G_0) const {
    if (CI_0 <= 0.0 || G_0 <= 0.0 || CI <= 0.0) {
        return 0.0;
    }

    const double k_G = CI_0 / (G_0 * G_0);
    if (k_G <= 0.0) {
        return 0.0;
    }

    return std::sqrt(CI / k_G);
}

double KevinHallModel::compute_ECF(double delta_Na, double CI, double CI_0, double ECF_0) const {
    if (CI_0 <= 0.0) {
        return ECF_0 + delta_Na / xi_Na;
    }

    return ECF_0 + (delta_Na - xi_CI * (1.0 - CI / CI_0)) / xi_Na;
}

double KevinHallModel::compute_delta(int age, core::Gender sex, double PAL, double BW,
                                     double H) const {
    if (BW <= 0.0) {
        return 0.0;
    }

    double RMR = 9.99 * BW + 6.25 * H - 4.92 * age;
    RMR += sex == core::Gender::male ? 5.0 : -161.0;
    RMR *= 4.184;

    return ((1.0 - beta_TEF) * PAL - 1.0) * RMR / BW;
}

double KevinHallModel::compute_EE(double BW, double F, double L, double EI, double K, double delta,
                                  double x) const {
    return (K + gamma_F * F + gamma_L * L + delta * BW + beta_TEF + beta_AT +
            x * (EI - rho_G * 0.0)) /
           (1.0 + x);
}

void KevinHallModel::compute_bmi(Person &person) const {
    const auto w = person.risk_factors.at("Weight"_id);
    const auto h = person.risk_factors.at("Height"_id) / 100.0;
    person.risk_factors["BMI"_id] = w / (h * h);
}

void KevinHallModel::initialise_weight(RuntimeContext &context, Person &person) const {
    const double ei_expected =
        get_expected(context, person.gender, person.age, "EnergyIntake"_id, std::nullopt, true);
    const double pa_expected = get_expected(context, person.gender, person.age, "PhysicalActivity"_id,
                                            std::nullopt, false);

    if (!person.risk_factors.contains("EnergyIntake"_id)) {
        throw core::InternalError("Missing 'EnergyIntake' in initialise_weight");
    }
    if (!person.risk_factors.contains("PhysicalActivity"_id)) {
        throw core::InternalError("Missing 'PhysicalActivity' in initialise_weight");
    }

    const double ei_actual = person.risk_factors.at("EnergyIntake"_id);
    const double pa_actual = person.risk_factors.at("PhysicalActivity"_id);

    double epa_expected = 1.0;
    if (pa_expected > 0.0) {
        epa_expected = ei_expected / pa_expected;
    }

    double epa_actual = 1.0;
    if (pa_actual > 0.0) {
        epa_actual = ei_actual / pa_actual;
    }

    double epa_quantile = 1.0;
    if (epa_expected > 0.0) {
        epa_quantile = epa_actual / epa_expected;
    }

    const double w_expected =
        get_expected(context, person.gender, person.age, "Weight"_id, std::nullopt, true);
    const double w_quantile = get_weight_quantile(epa_quantile, person.gender);
    person.risk_factors["Weight"_id] = w_expected * w_quantile;
}

void KevinHallModel::adjust_weight(Person &person, double adjustment) const {
    const double H = person.risk_factors.at("Height"_id);
    const double BW_0 = person.risk_factors.at("Weight"_id);
    const double F_0 = person.risk_factors.at("BodyFat"_id);
    const double L_0 = person.risk_factors.at("LeanTissue"_id);
    const double G = person.risk_factors.at("Glycogen"_id);
    const double W = 2.7 * G;
    const double ECF_0 = person.risk_factors.at("ExtracellularFluid"_id);
    const double PAL = person.risk_factors.at("PhysicalActivity"_id);
    const double EI = person.risk_factors.at("EnergyIntake"_id);
    const double K_0 = person.risk_factors.at("Intercept_K"_id);

    const double C = 10.4 * rho_L / rho_F;

    const double p_0 = C / (C + F_0);
    const double x_0 = p_0 * eta_L / rho_L + (1.0 - p_0) * eta_F / rho_F;
    const double delta_0 = compute_delta(person.age, person.gender, PAL, BW_0, H);
    const double EE_0 = compute_EE(BW_0, F_0, L_0, EI, K_0, delta_0, x_0);

    const double BW = BW_0 + adjustment;
    const double denominator = BW_0 - G - W;
    const double ratio = denominator != 0.0 ? (BW - G - W) / denominator : 1.0;

    const double F = F_0 * ratio;
    const double L = L_0 * ratio;
    const double ECF = ECF_0 * ratio;

    const double p = C / (C + F);
    const double x = p * eta_L / rho_L + (1.0 - p) * eta_F / rho_F;
    const double delta = compute_delta(person.age, person.gender, PAL, BW, H);
    const double EE = compute_EE(BW, F, L, EI, K_0, delta, x);

    const double K = K_0 + (1.0 + x) * (EE_0 - EE);

    person.risk_factors.at("Weight"_id) = BW;
    person.risk_factors.at("BodyFat"_id) = F;
    person.risk_factors.at("LeanTissue"_id) = L;
    person.risk_factors.at("ExtracellularFluid"_id) = ECF;
    person.risk_factors.at("Intercept_K"_id) = K;
    person.risk_factors["EnergyExpenditure"_id] = EE;
}

double KevinHallModel::get_weight_quantile(double epa_quantile, core::Gender sex) const {
    auto epa_range = std::equal_range(epa_quantiles_.begin(), epa_quantiles_.end(), epa_quantile);
    auto epa_index = static_cast<double>(std::distance(epa_quantiles_.begin(), epa_range.first));
    epa_index += std::distance(epa_range.first, epa_range.second) / 2.0;
    const auto epa_percentile = epa_index / epa_quantiles_.size();

    const size_t weight_index_last = weight_quantiles_.at(sex).size() - 1;
    const auto weight_index = static_cast<size_t>(epa_percentile * weight_index_last);
    return weight_quantiles_.at(sex)[weight_index];
}

KevinHallAdjustmentTable
KevinHallModel::compute_mean_weight(Population &population,
                                    std::optional<std::unordered_map<core::Gender, double>> power,
                                    std::optional<unsigned> age) const {
    struct SumCount {
      public:
        void append(double value) noexcept {
            sum_ += value;
            count_++;
        }

        double mean() const noexcept { return count_ > 0 ? sum_ / count_ : 0.0; }

      private:
        double sum_{};
        int count_{};
    };

    auto sumcounts = UnorderedMap2d<core::Gender, int, SumCount>{};
    sumcounts.emplace_row(core::Gender::female, std::unordered_map<int, SumCount>{});
    sumcounts.emplace_row(core::Gender::male, std::unordered_map<int, SumCount>{});

    for (const auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        if (age.has_value() && person.age != age.value()) {
            continue;
        }

        double value;
        if (power.has_value()) {
            value = std::pow(person.risk_factors.at("Weight"_id), power.value()[person.gender]);
        } else {
            value = person.risk_factors.at("Weight"_id);
        }

        sumcounts.at(person.gender)[person.age].append(value);
    }

    auto means = KevinHallAdjustmentTable{};
    for (const auto &[sumcount_sex, sumcounts_by_sex] : sumcounts) {
        means.emplace_row(sumcount_sex, std::unordered_map<int, double>{});
        for (const auto &[sumcount_age, sumcount] : sumcounts_by_sex) {
            means.at(sumcount_sex)[sumcount_age] = sumcount.mean();
        }
    }

    return means;
}

void KevinHallModel::initialise_height(RuntimeContext &context, Person &person, double W_power_mean,
                                       Random &random) const {
    const double H_residual = random.next_normal(0.0, height_stddev_.at(person.gender));
    person.risk_factors["Height_residual"_id] = H_residual;

    update_height(context, person, W_power_mean);
}

void KevinHallModel::update_height(RuntimeContext &context, Person &person,
                                   double W_power_mean) const {
    const double W = person.risk_factors.at("Weight"_id);
    const double H_expected =
        get_expected(context, person.gender, person.age, "Height"_id, std::nullopt, false);
    const double H_residual = person.risk_factors.at("Height_residual"_id);
    const double stddev = height_stddev_.at(person.gender);
    const double slope = height_slope_.at(person.gender);

    const double exp_norm_mean = std::exp(0.5 * stddev * stddev);
    const double safe_W_power_mean = W_power_mean != 0.0 ? W_power_mean : 1.0;
    const double H =
        H_expected * (std::pow(W, slope) / safe_W_power_mean) * (std::exp(H_residual) / exp_norm_mean);

    person.risk_factors["Height"_id] = H;
}

KevinHallModelDefinition::KevinHallModelDefinition(
    std::unique_ptr<RiskFactorSexAgeTable> expected,
    std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
    std::unique_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
    std::unordered_map<core::Identifier, double> energy_equation,
    std::unordered_map<core::Identifier, core::DoubleInterval> nutrient_ranges,
    std::unordered_map<core::Identifier, std::map<core::Identifier, double>> nutrient_equations,
    std::unordered_map<core::Identifier, std::optional<double>> food_prices,
    std::unordered_map<core::Gender, std::vector<double>> weight_quantiles,
    std::vector<double> epa_quantiles, std::unordered_map<core::Gender, double> height_stddev,
    std::unordered_map<core::Gender, double> height_slope)
    : RiskFactorAdjustableModelDefinition{std::move(expected), std::move(expected_trend),
                                          std::move(trend_steps)},
      energy_equation_{std::move(energy_equation)}, nutrient_ranges_{std::move(nutrient_ranges)},
      nutrient_equations_{std::move(nutrient_equations)}, food_prices_{std::move(food_prices)},
      weight_quantiles_{std::move(weight_quantiles)}, epa_quantiles_{std::move(epa_quantiles)},
      height_stddev_{std::move(height_stddev)}, height_slope_{std::move(height_slope)} {
    if (energy_equation_.empty()) {
        throw core::InternalError("Energy equation mapping is empty");
    }
    if (nutrient_ranges_.empty()) {
        throw core::InternalError("Nutrient ranges mapping is empty");
    }
    if (nutrient_equations_.empty()) {
        throw core::InternalError("Nutrient equation mapping is empty");
    }
    if (food_prices_.empty()) {
        throw core::InternalError("Food prices mapping is empty");
    }
    if (weight_quantiles_.empty()) {
        throw core::InternalError("Weight quantiles mapping is empty");
    }
    if (epa_quantiles_.empty()) {
        throw core::InternalError("Energy Physical Activity quantiles mapping is empty");
    }
    if (height_stddev_.empty()) {
        throw core::InternalError("Height standard deviation mapping is empty");
    }
    if (height_slope_.empty()) {
        throw core::InternalError("Height slope mapping is empty");
    }
}

std::unique_ptr<RiskFactorModel> KevinHallModelDefinition::create_model() const {
    return std::make_unique<KevinHallModel>(expected_, expected_trend_, trend_steps_,
                                            energy_equation_, nutrient_ranges_, nutrient_equations_,
                                            food_prices_, weight_quantiles_, epa_quantiles_,
                                            height_stddev_, height_slope_);
}

} // namespace hgps
// NOLINTEND(readability-convert-member-functions-to-static)