// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{risk_factor_model,riskfactor,risk_factor_adjustable_model}.h.
#pragma once

#include "core/identifier.h"
#include "core/interval.h"
#include "model/containers.h"
#include "model/module.h"
#include "sim/scenario.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief The two slots a risk-factor model can fill.
enum class RiskFactorModelType : std::uint8_t {
    /// @brief Generates the initial cohort's factor values.
    Static,
    /// @brief Moves them year on year.
    Dynamic,
};

/// @brief Expected factor values by sex and factor, indexed by age.
using SexAgeFactorTable = Map2d<core::Gender, core::Identifier, std::vector<double>>;

/// @brief How expected values move over time.
enum class TrendType : std::uint8_t {
    /// @brief No trend.
    Null,
    /// @brief The ultra-processed-food trend: expected *= factor^elapsed, capped by trend steps.
    UpfTrend,
    /// @brief The income trend: expected *= trend * exp(decay * elapsed), from the second year.
    IncomeTrend,
};

/// @brief The demographic attributes a risk-factor model gives people, beyond the risk factors
///        named in the config's `risk_factors` list.
///
/// This is what decides whether the corresponding output channels exist, and it is a property of
/// the model family rather than of the population. The baseline decides by looking at the first
/// 1,000 people and adding a channel if any of them has the attribute set, so its output's column
/// set depends on the contents of a sample of the cohort; a project requirement alone is not
/// enough either, because the defaults switch income and physical activity on for every config
/// including the HLM ones, whose models assign neither — six columns of zeros.
struct AssignedAttributes {
    /// @brief `person.income`, the categorical income band.
    bool income_category{false};
    /// @brief A continuous `income` risk factor.
    bool income{false};
    /// @brief A `physical_activity` risk factor distinct from the config's `PA`.
    bool physical_activity{false};
    bool region{false};
    bool ethnicity{false};
    /// @brief `person.sector`, urban or rural.
    bool sector{false};
};

/// @brief A risk-factor model: generates factor values, then updates them each year.
class RiskFactorModel {
  public:
    RiskFactorModel() = default;
    virtual ~RiskFactorModel() = default;
    RiskFactorModel(const RiskFactorModel &) = delete;
    RiskFactorModel &operator=(const RiskFactorModel &) = delete;
    RiskFactorModel(RiskFactorModel &&) = delete;
    RiskFactorModel &operator=(RiskFactorModel &&) = delete;

    virtual RiskFactorModelType type() const noexcept = 0;
    virtual std::string name() const noexcept = 0;

    /// @brief Gives the population its factor values.
    virtual void generate_risk_factors(RuntimeContext &context,
                                       sim::ScenarioJournal &journal) = 0;

    /// @brief Moves them one year.
    virtual void update_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) = 0;

    /// @brief What this model assigns besides the declared risk factors. Nothing, by default.
    virtual AssignedAttributes assigns() const noexcept { return {}; }

    /// @brief Risk factors this model creates that `modelling.risk_factors` does not declare.
    ///
    /// The FINCH static model generates twenty-one *food group* intakes — `FoodCarbohydrate` and
    /// the rest — which the config's risk factor list does not mention, because what it declares
    /// are the *nutrients* the Kevin Hall model derives from them. They are real risk factors on
    /// a person all the same, so a later model's coefficients may name them, and load-time
    /// validation has to know about them or it would reject a correct model file.
    virtual std::vector<core::Identifier> generated_factors() const { return {}; }
};

/// @brief Which people a calibration pass covers, and against which expected values.
///
/// The FINCH shape calibrates each income quintile against its own FactorsMean tables, so a pass
/// needs both an alternative expected table and a filter. The defaults give the ordinary
/// whole-population pass against the model's own table.
///
/// At namespace scope rather than nested in the model, because a default argument of a member
/// function may not use a nested class's default member initialiser.
struct AdjustmentScope {
    /// @brief An expected table to use instead of the model's own, or null.
    const SexAgeFactorTable *expected_table{nullptr};

    /// @brief When set, only people carrying this adjustment stratum take part — both in the
    ///        simulated mean and in the shift.
    ///
    /// The deltas travel to the intervention through the journal, which replays a year's passes
    /// in the order they were recorded; membership of a stratum is decided locally in each
    /// scenario, exactly as in the baseline.
    std::optional<std::size_t> income_stratum;
};

/// @brief A model that calibrates simulated means to the FactorsMean tables.
///
/// The adjustment is computed by the baseline scenario and reused by the intervention, so the two
/// futures are calibrated identically. The baseline passes it over a channel with a timeout; here
/// it goes in the journal (ADR 0009).
class AdjustableRiskFactorModel : public RiskFactorModel {
  public:
    /// @param trend       For the UPF trend, `ExpectedTrend` per factor; for the income trend,
    ///                     `ExpectedIncomeTrend`.
    /// @param trend_steps  How many years the UPF trend keeps compounding for. Unused by the
    ///                     income trend, which decays instead of stopping.
    /// @param decay        The income trend's per-factor exponential decay. Null otherwise.
    AdjustableRiskFactorModel(
        std::shared_ptr<const SexAgeFactorTable> expected,
        std::shared_ptr<const std::map<core::Identifier, double>> trend,
        std::shared_ptr<const std::map<core::Identifier, int>> trend_steps,
        TrendType trend_type = TrendType::Null,
        std::shared_ptr<const std::map<core::Identifier, double>> decay = nullptr);

    /// @brief The expected value of a factor for a sex and age, with any trend applied.
    ///
    /// Virtual because the Kevin Hall model derives some of its expected values rather than
    /// reading them: a nutrient's from the expected food intakes, a weight's from the expected
    /// energy intake, height and physical activity.
    ///
    /// @throws diag::InternalError if the FactorsMean table has no column for the factor — which
    ///         is a model definition that names a factor its own calibration data lacks.
    virtual double get_expected(RuntimeContext &context, core::Gender sex, int age,
                                const core::Identifier &factor,
                                std::optional<core::DoubleInterval> range,
                                bool apply_trend) const;

    /// @brief The number of years a factor's trend keeps being applied for.
    int get_trend_steps(const core::Identifier &factor) const;

    /// @brief Shifts every person's factors so the simulated means match the expected ones.
    void adjust_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal,
                             const std::vector<core::Identifier> &factors,
                             const std::vector<core::DoubleInterval> *ranges, bool apply_trend,
                             const AdjustmentScope &scope = {}) const;

    /// @brief The factors whose zeros are excluded from the simulated mean, because a two-stage
    ///        model treats zero as "not applicable" rather than as a low value.
    void set_logistic_factors(std::vector<core::Identifier> factors);

  protected:
    const SexAgeFactorTable &expected() const noexcept { return *expected_; }

    /// @brief The same lookup against an arbitrary table — an income stratum's, say.
    double expected_from(const SexAgeFactorTable &table, RuntimeContext &context,
                         core::Gender sex, int age, const core::Identifier &factor,
                         std::optional<core::DoubleInterval> range, bool apply_trend) const;

    TrendType trend_type() const noexcept { return trend_type_; }

    /// @brief The simulated mean of each factor by sex and age. NaN where nobody contributed.
    static SexAgeFactorTable
    calculate_simulated_mean(const Population &population, core::IntegerInterval age_range,
                             const std::vector<core::Identifier> &factors,
                             const std::vector<core::Identifier> &logistic_factors,
                             std::optional<std::size_t> income_stratum = std::nullopt);

  private:
    std::shared_ptr<const SexAgeFactorTable> expected_;
    std::shared_ptr<const std::map<core::Identifier, double>> trend_;
    std::shared_ptr<const std::map<core::Identifier, int>> trend_steps_;
    std::shared_ptr<const std::map<core::Identifier, double>> decay_;
    TrendType trend_type_{TrendType::Null};
    std::vector<core::Identifier> logistic_factors_;

    sim::AdjustmentTable calculate_adjustments(RuntimeContext &context,
                                                const std::vector<core::Identifier> &factors,
                                                const std::vector<core::DoubleInterval> *ranges,
                                                bool apply_trend,
                                                const AdjustmentScope &scope) const;
};

/// @brief Hosts the static and dynamic models, and runs whichever the year calls for.
class RiskFactorHostModule final : public SimulationModule {
  public:
    RiskFactorHostModule() = delete;

    RiskFactorHostModule(std::unique_ptr<RiskFactorModel> static_model,
                         std::unique_ptr<RiskFactorModel> dynamic_model,
                         sim::ScenarioJournal &journal);

    ModuleType type() const noexcept override { return ModuleType::RiskFactor; }
    const std::string &name() const noexcept override { return name_; }

    std::size_t size() const noexcept;
    bool contains(RiskFactorModelType model_type) const noexcept;

    /// @brief The static model generates the initial cohort's factors.
    void initialise_population(RuntimeContext &context) override;

    /// @brief The static model generates newborns' factors; the dynamic model moves everyone
    ///        else's. In that order, because the dynamic model's calibration includes the
    ///        newborns.
    void update_population(RuntimeContext &context);

  private:
    std::unique_ptr<RiskFactorModel> static_model_;
    std::unique_ptr<RiskFactorModel> dynamic_model_;
    sim::ScenarioJournal *journal_;
    std::string name_{"RiskFactor"};
};

} // namespace hgps::model
