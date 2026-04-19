#pragma once

#include "hgps_core/diagnostics/internal_error.h"
#include "hgps_core/types/identifier.h"

#include "data/mapping.h"
#include "risk_factor_adjustable_model.h"
#include "types/interfaces.h"
#include "utils/map2d.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace hgps {

using KevinHallAdjustmentTable = UnorderedMap2d<core::Gender, int, double>;

class KevinHallModel final : public RiskFactorAdjustableModel {
  public:
    KevinHallModel(
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
        const std::unordered_map<core::Gender, double> &height_slope);

    RiskFactorModelType type() const noexcept override;

    std::string name() const override;

    void generate_risk_factors(RuntimeContext &context) override;

    void update_risk_factors(RuntimeContext &context) override;

  private:
    void update_newborns(RuntimeContext &context) const;

    void update_non_newborns(RuntimeContext &context) const;

    void initialise_nutrient_intakes(Person &person) const;

    void update_nutrient_intakes(Person &person) const;

    void compute_nutrient_intakes(Person &person) const;

    void initialise_energy_intake(Person &person) const;

    void update_energy_intake(Person &person) const;

    void compute_energy_intake(Person &person) const;

    double compute_G(double CI, double CI_0, double G_0) const;

    double compute_ECF(double delta_Na, double CI, double CI_0, double ECF_0) const;

    double compute_delta(int age, core::Gender sex, double PAL, double BW, double H) const;

    double compute_EE(double BW, double F, double L, double EI, double K, double delta,
                      double x) const;

    void compute_bmi(Person &person) const;

    void initialise_weight(RuntimeContext &context, Person &person) const;

    void initialise_kevin_hall_state(Person &person,
                                     std::optional<double> adjustment = std::nullopt) const;

    void kevin_hall_run(Person &person) const;

    void adjust_weight(Person &person, double adjustment) const;

    KevinHallAdjustmentTable receive_weight_adjustments(RuntimeContext &context) const;

    void send_weight_adjustments(RuntimeContext &context,
                                 KevinHallAdjustmentTable &&adjustments) const;

    KevinHallAdjustmentTable
    compute_weight_adjustments(RuntimeContext &context,
                               std::optional<unsigned> age = std::nullopt) const;

    double get_expected(RuntimeContext &context, core::Gender sex, int age,
                        const core::Identifier &factor, OptionalRange range,
                        bool apply_trend) const override;

    double get_weight_quantile(double epa_quantile, core::Gender sex) const;

    KevinHallAdjustmentTable compute_mean_weight(
        Population &population,
        std::optional<std::unordered_map<core::Gender, double>> power = std::nullopt,
        std::optional<unsigned> age = std::nullopt) const;

    void initialise_height(RuntimeContext &context, Person &person, double W_power_mean,
                           Random &random) const;

    void update_height(RuntimeContext &context, Person &person, double W_power_mean) const;

    const std::unordered_map<core::Identifier, double> &energy_equation_;
    const std::unordered_map<core::Identifier, core::DoubleInterval> &nutrient_ranges_;
    const std::unordered_map<core::Identifier, std::map<core::Identifier, double>>
        &nutrient_equations_;
    const std::unordered_map<core::Identifier, std::optional<double>> &food_prices_;
    const std::unordered_map<core::Gender, std::vector<double>> &weight_quantiles_;
    const std::vector<double> &epa_quantiles_;
    const std::unordered_map<core::Gender, double> &height_stddev_;
    const std::unordered_map<core::Gender, double> &height_slope_;

    static constexpr int kevin_hall_age_min = 19;
    static constexpr double rho_F = 39.5e3;
    static constexpr double rho_L = 7.6e3;
    static constexpr double rho_G = 17.6e3;
    static constexpr double gamma_F = 13.0;
    static constexpr double gamma_L = 92.0;
    static constexpr double eta_F = 750.0;
    static constexpr double eta_L = 960.0;
    static constexpr double beta_TEF = 0.1;
    static constexpr double beta_AT = 0.14;
    static constexpr double xi_Na = 3000.0;
    static constexpr double xi_CI = 4000.0;
};

class KevinHallModelDefinition final : public RiskFactorAdjustableModelDefinition {
  public:
    KevinHallModelDefinition(
        std::unique_ptr<RiskFactorSexAgeTable> expected,
        std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
        std::unique_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
        std::unordered_map<core::Identifier, double> energy_equation,
        std::unordered_map<core::Identifier, core::DoubleInterval> nutrient_ranges,
        std::unordered_map<core::Identifier, std::map<core::Identifier, double>> nutrient_equations,
        std::unordered_map<core::Identifier, std::optional<double>> food_prices,
        std::unordered_map<core::Gender, std::vector<double>> weight_quantiles,
        std::vector<double> epa_quantiles, std::unordered_map<core::Gender, double> height_stddev,
        std::unordered_map<core::Gender, double> height_slope);

    std::unique_ptr<RiskFactorModel> create_model() const override;

  private:
    std::unordered_map<core::Identifier, double> energy_equation_;
    std::unordered_map<core::Identifier, core::DoubleInterval> nutrient_ranges_;
    std::unordered_map<core::Identifier, std::map<core::Identifier, double>> nutrient_equations_;
    std::unordered_map<core::Identifier, std::optional<double>> food_prices_;
    std::unordered_map<core::Gender, std::vector<double>> weight_quantiles_;
    std::vector<double> epa_quantiles_;
    std::unordered_map<core::Gender, double> height_stddev_;
    std::unordered_map<core::Gender, double> height_slope_;
};

} // namespace hgps