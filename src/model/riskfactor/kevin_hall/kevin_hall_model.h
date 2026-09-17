// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/kevin_hall_model.{h,cpp}.
#pragma once

#include "model/containers.h"
#include "model/riskfactor/risk_factor_model.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief The height regression's two parameters, per sex and income stratum.
///
/// Height is modelled as `expected × (W^slope / mean(W^slope)) × exp(residual) / exp(σ²/2)`: a
/// person's height tracks the population's weight-to-height relationship, with a lifelong
/// residual. `slope` is the exponent and `stddev` the residual's spread.
struct HeightModelParams {
    double slope{};
    double stddev{};
};

/// @brief A weight adjustment per sex and age.
using WeightAdjustmentTable = Map2d<core::Gender, int, double>;

/// @brief Everything the Kevin Hall energy-balance model is built from.
struct KevinHallParameters {
    /// @brief Energy per unit of each nutrient, in kJ.
    std::map<core::Identifier, double> energy_equation;

    /// @brief The plausible range of each nutrient.
    std::map<core::Identifier, core::DoubleInterval> nutrient_ranges;

    /// @brief Each food group's nutrient content, per unit of the food.
    std::map<core::Identifier, std::map<core::Identifier, double>> nutrient_equations;

    /// @brief Each food group's unit price, where the pack gives one.
    std::map<core::Identifier, std::optional<double>> food_prices;

    /// @brief Sorted weight quantiles per sex, one curve per income adjustment stratum. A pack
    ///        with a single curve broadcasts it to every stratum.
    std::map<core::Gender, std::vector<std::vector<double>>> weight_quantiles;

    /// @brief The sorted energy-to-physical-activity quantiles the weight curve is read against.
    std::vector<double> epa_quantiles;

    /// @brief Height parameters per sex, one row per income adjustment stratum.
    std::map<core::Gender, std::vector<HeightModelParams>> height_params;

    /// @brief The configured range of `Weight`, used to check the energy balance has not produced
    ///        an impossible body.
    std::optional<core::DoubleInterval> weight_range;
};

/// @brief The `KevinHall` dynamic model: a physiological energy-balance model.
///
/// Each year a person's food intakes give their nutrient intakes, the nutrients give their energy
/// intake, and the change in energy intake moves body fat, lean tissue, glycogen and extracellular
/// fluid to a new steady state — from which weight, and then BMI, follow. Children below
/// `kAdultAge` are not run through the balance at all: their weight comes from the weight-quantile
/// curve, because growth dominates energy balance at those ages.
///
/// The weight adjustment that keeps the population mean on the FactorsMean table is computed by
/// the baseline scenario and replayed by the intervention through the journal, exactly as the
/// risk-factor calibration is.
class KevinHallModel final : public AdjustableRiskFactorModel {
  public:
    KevinHallModel(std::shared_ptr<const SexAgeFactorTable> expected,
                   std::shared_ptr<const std::map<core::Identifier, double>> trend,
                   std::shared_ptr<const std::map<core::Identifier, int>> trend_steps,
                   std::shared_ptr<const KevinHallParameters> parameters);

    RiskFactorModelType type() const noexcept override { return RiskFactorModelType::Dynamic; }
    std::string name() const noexcept override { return "Dynamic"; }

    void generate_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) override;
    void update_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) override;

    /// @brief Nutrients, energy intake, weight and height are derived rather than looked up.
    double get_expected(RuntimeContext &context, core::Gender sex, int age,
                        const core::Identifier &factor,
                        std::optional<core::DoubleInterval> range,
                        bool apply_trend) const override;

    /// @brief Below this age the energy balance is not run; weight comes from the quantile curve.
    static constexpr int kAdultAge = 19;

    // --- the model's physiological constants, in kJ and kg -----------------------------------
    static constexpr double kRhoFat = 39.5e3;    ///< energy content of fat, kJ/kg
    static constexpr double kRhoLean = 7.6e3;    ///< energy content of lean tissue, kJ/kg
    static constexpr double kRhoGlycogen = 17.6e3;
    static constexpr double kGammaFat = 13.0;    ///< resting metabolic rate, kJ/kg/day
    static constexpr double kGammaLean = 92.0;
    static constexpr double kEtaFat = 750.0;     ///< synthesis cost, kJ/kg
    static constexpr double kEtaLean = 960.0;
    static constexpr double kBetaTef = 0.1;      ///< thermic effect of food, unitless
    static constexpr double kBetaAt = 0.14;      ///< adaptive thermogenesis, unitless
    static constexpr double kXiSodium = 3000.0;  ///< sodium per litre of fluid, mg/L/day
    static constexpr double kXiCarbohydrate = 4000.0;

    /// @brief The weight quantile for an energy/physical-activity quantile. Exposed for tests.
    ///
    /// The E/PA quantile is placed in the sorted `epa_quantiles` — taking the midpoint of a run of
    /// equal values, so a curve with plateaus does not bias to one end — and the resulting
    /// percentile indexes the weight curve.
    double weight_quantile(double epa_quantile, const std::vector<double> &quantiles) const;

    /// @brief Glycogen from carbohydrate intake. Exposed for tests.
    static double compute_glycogen(double carbohydrate, double carbohydrate_0, double glycogen_0);

    /// @brief Extracellular fluid from the changes in sodium and carbohydrate. Exposed for tests.
    static double compute_extracellular_fluid(double delta_sodium, double carbohydrate,
                                              double carbohydrate_0, double fluid_0);

    /// @brief Energy cost per kg of body weight, from the Mifflin–St Jeor resting metabolic rate.
    ///        Exposed for tests.
    static double compute_delta(int age, core::Gender sex, double activity, double weight,
                                double height);

    /// @brief Energy expenditure. Exposed for tests.
    static double compute_expenditure(double weight, double fat, double lean, double intake,
                                      double intercept, double delta, double partition);

    /// @brief The height and weight parameters for a person's sex and income stratum.
    const HeightModelParams &height_params_for(const Person &person) const;
    const std::vector<double> &weight_quantiles_for(const Person &person) const;

  private:
    std::shared_ptr<const KevinHallParameters> parameters_;

    void initialise_nutrient_intakes(Person &person) const;
    void update_nutrient_intakes(Person &person) const;
    void compute_nutrient_intakes(Person &person) const;

    void initialise_energy_intake(Person &person) const;
    void update_energy_intake(Person &person) const;
    void compute_energy_intake(Person &person) const;

    void compute_bmi(Person &person) const;

    void initialise_weight(RuntimeContext &context, Person &person) const;
    void initialise_state(RuntimeContext &context, Person &person,
                          std::optional<double> adjustment = std::nullopt) const;
    void run_energy_balance(RuntimeContext &context, Person &person) const;
    void adjust_weight(RuntimeContext &context, Person &person, double adjustment) const;

    /// @brief Checks a weight against the configured range.
    ///
    /// Below the minimum is an error: a body that light is not a person the rest of the model can
    /// describe. Above the maximum is reported through the run metrics and the value kept — the
    /// baseline prints a warning to stdout and carries on, and refusing here would stop a run
    /// over one heavy person.
    void validate_weight(RuntimeContext &context, const Person &person,
                         std::string_view phase) const;

    void initialise_height(RuntimeContext &context, Person &person, double weight_power_mean,
                           rng::RandomSource &random) const;
    void update_height(RuntimeContext &context, Person &person, double weight_power_mean) const;

    void update_newborns(RuntimeContext &context, sim::ScenarioJournal &journal) const;
    void update_others(RuntimeContext &context, sim::ScenarioJournal &journal) const;

    /// @brief `expected - simulated` weight by sex and age, for all ages or just one.
    WeightAdjustmentTable compute_weight_adjustments(RuntimeContext &context,
                                                     std::optional<unsigned int> age) const;

    /// @brief The baseline's adjustments, computed or replayed.
    WeightAdjustmentTable weight_adjustments(RuntimeContext &context,
                                             sim::ScenarioJournal &journal,
                                             std::optional<unsigned int> age) const;

    /// @brief The mean of `weight^slope` by sex and age, which the height model divides by.
    WeightAdjustmentTable mean_weight_power(const Population &population,
                                            std::optional<unsigned int> age) const;
};

} // namespace hgps::model
