// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/static_linear_model.{h,cpp} — 2,615 lines there, split here into one
//         translation unit per concern (docs/decisions/0019-split-the-monolith-translation-units.md).
#pragma once

#include "core/income_category_layout.h"
#include "core/matrix.h"
#include "model/containers.h"
#include "model/riskfactor/linear_model.h"
#include "model/riskfactor/risk_factor_model.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief The physical-activity model, in either of its two shapes.
///
/// `simple` draws a log-normal around the expected value — the shape the India pack uses.
/// `continuous` evaluates a regression read from a two-column CSV and adds normal noise — the
/// shape the FINCH pack uses. Which one runs is decided by
/// `project_requirements.physical_activity.type` and by nothing else: the baseline falls back to
/// a default model when the configured one is missing, which makes a typo in the config produce
/// a different model rather than an error (docs/deviations.md).
struct PhysicalActivityModel {
    /// @brief "simple" or "continuous".
    std::string type{"simple"};

    /// @brief The regression. Only the continuous shape uses it.
    LinearModelParams linear;

    double stddev{0.0};

    /// @brief From the CSV's `min`/`max` rows, used only when the config's mapping has no range
    ///        for `PhysicalActivity`.
    std::optional<double> min_value;
    std::optional<double> max_value;
};

/// @brief One income stratum's own FactorsMean tables.
struct IncomeStratumExpected {
    std::string id;
    std::shared_ptr<const SexAgeFactorTable> expected;
};

/// @brief Everything the static linear model is built from.
///
/// One struct rather than the baseline's 30-argument constructor, which takes eleven defaulted
/// `shared_ptr` parameters in an order no caller can check.
struct StaticLinearParameters {
    /// @brief The factor order, which is the column order of the correlation matrix CSV.
    ///
    /// It is load-bearing: the Cholesky factor, the residual vector and every per-factor vector
    /// below are indexed by it, so a different order is a different model.
    std::vector<core::Identifier> names;

    std::vector<LinearModelParams> models;
    std::vector<core::DoubleInterval> ranges;
    std::vector<double> lambda;
    std::vector<double> stddev;

    /// @brief The lower-triangular Cholesky factor of the residual correlation matrix.
    core::Matrix cholesky;

    std::vector<LinearModelParams> policy_models;
    std::vector<core::DoubleInterval> policy_ranges;
    core::Matrix policy_cholesky;

    /// @brief False when every policy coefficient and intercept is zero, in which case the policy
    ///        residuals are not drawn and no policy is computed or applied.
    ///
    /// The baseline makes the same optimisation. It is visible in the results — skipping the
    /// residual draw shifts the whole random stream — so it is part of the model rather than an
    /// implementation detail, and the loader records it in the run metadata.
    bool has_active_policies{true};

    /// @brief One per factor, in `names` order. An entry with no coefficients means that factor
    ///        has no first stage and is modelled by the Box-Cox transform alone.
    std::vector<LinearModelParams> logistic_models;

    TrendType trend_type{TrendType::Null};
    std::vector<LinearModelParams> trend_models;
    std::vector<core::DoubleInterval> trend_ranges;
    std::vector<double> trend_lambda;
    std::map<core::Identifier, double> expected_trend_boxcox;

    std::vector<LinearModelParams> income_trend_models;
    std::vector<core::DoubleInterval> income_trend_ranges;
    std::vector<double> income_trend_lambda;
    std::map<core::Identifier, double> expected_income_trend_boxcox;
    std::map<core::Identifier, int> income_trend_steps;
    std::map<core::Identifier, double> income_trend_decay_factors;

    /// @brief How much of a person's residual is redrawn each year, in [0, 1].
    double info_speed{};

    /// @brief Rural share by age group ("Under18", "Over18") and sex. Empty means the model does
    ///        not assign a sector.
    std::map<core::Identifier, GenderValue<double>> rural_prevalence;

    /// @brief True for the FINCH shape: income is a continuous regression, and the categories are
    ///        assigned afterwards by rank.
    bool continuous_income{false};
    LinearModelParams continuous_income_model;

    /// @brief The categorical shape: one logit model per income category.
    std::map<core::Income, LinearModelParams> income_models;

    core::IncomeCategoryLayout income_layout;

    bool income_enabled{true};
    bool physical_activity_enabled{false};
    PhysicalActivityModel physical_activity;

    /// @brief Per-stratum FactorsMean tables, in the order the config lists them.
    std::vector<IncomeStratumExpected> income_stratum_expected;
    bool income_stratum_adjustment_enabled{false};
    std::size_t adjustment_income_stratum_count{0};

    /// @brief Which sex the `gender2` regression row scores 1 for.
    core::Gender gender2_indicator{core::Gender::male};

    /// @brief `demographics.max_age_for_linear_models`: the age/age²/age³ terms are capped here.
    std::optional<int> max_age_for_linear_models;

    /// @brief The project requirements that gate the adjustment passes, copied so the model does
    ///        not reach back into the config at run time.
    bool adjust_factors_to_mean{true};
    bool factors_trended{true};
    bool adjust_income_to_mean{false};
    bool income_trended{false};
    bool adjust_physical_activity_to_mean{false};
    bool physical_activity_trended{false};
    bool trend_enabled{false};
};

/// @brief Resolves every coefficient name in every model these parameters hold to a risk-factor
///        index, once.
///
/// Called by the loader at the last point the parameters are mutable, because `StaticLinearModel`
/// holds them by `shared_ptr<const>`. `model::resolve_predictors` says why it cannot change an
/// answer; this is that function applied to each of the eight places a `LinearModelParams` lives in
/// here, so that adding a ninth is a compile error rather than a silent slow path.
void resolve_static_linear_predictors(StaticLinearParameters &parameters);

/// @brief The `StaticLinear` model: the FINCH and India static risk-factor family.
///
/// Generates a person's factors from a per-factor linear model plus a correlated residual, put
/// through an inverse Box-Cox transform and scaled by the expected value for their age and sex.
/// On top of that sit five optional pieces, each switched on by `project_requirements`:
///
///  - a **two-stage** first step, in which a logistic regression decides whether the factor is
///    zero at all before the Box-Cox step computes how much;
///  - **income**, either as a categorical draw over softmax probabilities or as a continuous
///    regression whose values are ranked into categories afterwards;
///  - **physical activity**, simple or continuous;
///  - **sector**, urban or rural;
///  - **trends**, either the ultra-processed-food trend or the income trend.
///
/// and calibration of all of it to the FactorsMean tables, optionally stratified by income
/// quintile.
class StaticLinearModel final : public AdjustableRiskFactorModel {
  public:
    StaticLinearModel(std::shared_ptr<const SexAgeFactorTable> expected,
                      std::shared_ptr<const std::map<core::Identifier, double>> trend,
                      std::shared_ptr<const std::map<core::Identifier, int>> trend_steps,
                      std::shared_ptr<const StaticLinearParameters> parameters,
                      std::shared_ptr<const std::map<core::Identifier, double>> decay = nullptr);

    RiskFactorModelType type() const noexcept override { return RiskFactorModelType::Static; }
    std::string name() const noexcept override { return "Static"; }

    AssignedAttributes assigns() const noexcept override;

    /// @brief The factor names taken from the correlation matrix's columns.
    std::vector<core::Identifier> generated_factors() const override;

    /// @brief Generates the whole cohort's factors, then calibrates them.
    void generate_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) override;

    /// @brief Generates newborns' factors, moves everyone else's, then calibrates.
    void update_risk_factors(RuntimeContext &context, sim::ScenarioJournal &journal) override;

    /// @brief The inverse Box-Cox transform, exposed for its unit tests.
    ///
    /// Returns 0 rather than a NaN or an infinity for an argument the transform cannot take —
    /// `lambda * factor + 1 <= 0`, or an overflow — which is the baseline's behaviour and is
    /// what keeps a heavy residual tail from poisoning a whole run.
    static double inverse_box_cox(double factor, double lambda);

    /// @brief The rank-bucket split used for income categories and for adjustment strata.
    ///
    /// Sorts the active population by its `income` risk factor, breaking ties by slot index so
    /// the order is total, and gives rank r the bucket `(r * buckets) / n`. Exposed for tests.
    /// @return one bucket index per active person, in slot order; empty if nobody has an income.
    static std::vector<std::pair<std::size_t, std::size_t>>
    equal_rank_buckets(const Population &population, std::size_t buckets);

    /// @brief The cut points of an ordered empirical CDF: `buckets - 1` values, the i-th being
    ///        the `(i+1)/buckets` quantile of the population's income. Exposed for tests.
    static std::vector<double> income_percentile_thresholds(const Population &population,
                                                            std::size_t buckets);

    /// @brief Softmax probabilities of a set of logits, computed with the maximum subtracted.
    ///
    /// The baseline exponentiates the raw logits, which overflows to infinity for a logit above
    /// about 709 and then divides infinity by infinity. Subtracting the maximum first is the
    /// standard fix and changes nothing when the logits are small. Exposed for tests.
    static std::vector<double> softmax(std::span<const double> logits);

  private:
    std::shared_ptr<const StaticLinearParameters> parameters_;

    /// @brief The `<factor>_residual`, `<factor>_policy`, `<factor>_trend` … names, built once.
    ///
    /// Each is a vector parallel to `parameters_->names`. They used to be built by string
    /// concatenation inside the per-person loops — `factor.to_string() + "_residual"`, then an
    /// `Identifier` constructed from it, which validates every character. On `KevinHall_FINCH`
    /// that is thirty-four factors for every person in every year of both scenarios, and
    /// `Identifier::validate_identifier` and `chars::is_alnum` were 8.3% of the profile
    /// ([docs/performance.md](../../../../docs/performance.md)).
    struct DerivedKeys {
        std::vector<core::Identifier> residual;
        std::vector<core::Identifier> policy_residual;
        std::vector<core::Identifier> policy;
        std::vector<core::Identifier> trend;
        std::vector<core::Identifier> income_trend;
    };
    DerivedKeys derived_keys_;

    // --- factors.cpp
    /// @brief The evaluation options for the income, physical-activity and logistic regressions:
    ///        the gender2 indicator, and no age cap.
    LinearModelEvalOptions eval_options(const Person &person) const;

    /// @brief The same, plus `min(age, max_age_for_linear_models)`, for the risk factor, policy
    ///        and trend models — the three the cap applies to.
    LinearModelEvalOptions capped_eval_options(const Person &person) const;
    std::vector<double> compute_residuals(rng::RandomSource &random,
                                          const core::Matrix &cholesky) const;
    std::vector<double> compute_linear_models(RuntimeContext &context, const Person &person,
                                              const std::vector<LinearModelParams> &models) const;
    double zero_probability(const Person &person, std::size_t factor_index) const;
    void initialise_factors(RuntimeContext &context, Person &person,
                            rng::RandomSource &random) const;
    void update_factors(RuntimeContext &context, Person &person,
                        rng::RandomSource &random) const;

    // --- income.cpp
    void initialise_sector(Person &person, rng::RandomSource &random) const;
    void update_sector(Person &person, rng::RandomSource &random) const;
    double continuous_income_of(const Person &person, rng::RandomSource &random) const;
    void initialise_income(RuntimeContext &context, Person &person,
                           rng::RandomSource &random) const;
    void update_income(RuntimeContext &context, Person &person, rng::RandomSource &random) const;
    static void assign_income_categories(Population &population,
                                         const core::IncomeCategoryLayout &layout);
    static void assign_adjustment_strata(Population &population, std::size_t buckets);

    // --- physical_activity.cpp
    void initialise_physical_activity(RuntimeContext &context, Person &person,
                                      rng::RandomSource &random) const;

    // --- policies.cpp
    void initialise_policies(RuntimeContext &context, Person &person, rng::RandomSource &random,
                             bool intervene) const;
    void update_policies(RuntimeContext &context, Person &person, bool intervene) const;
    void apply_policies(Person &person, bool intervene) const;

    // --- trend.cpp
    void initialise_trends(RuntimeContext &context, Person &person) const;
    void update_trends(RuntimeContext &context, Person &person) const;
    void initialise_upf_trends(RuntimeContext &context, Person &person) const;
    void update_upf_trends(RuntimeContext &context, Person &person) const;
    void initialise_income_trends(RuntimeContext &context, Person &person) const;
    void update_income_trends(RuntimeContext &context, Person &person) const;

    // --- model.cpp
    /// @brief The factors calibration runs over: the declared ones, plus income and physical
    ///        activity when the requirements say to calibrate them and the FactorsMean tables
    ///        have a column for them.
    std::pair<std::vector<core::Identifier>, std::vector<core::DoubleInterval>>
    extended_factors(RuntimeContext &context, bool trended) const;

    /// @brief The one calibration pass, in whichever of its two shapes applies: a single pass
    ///        over the whole population, or income first and then one pass per income stratum.
    void calibrate(RuntimeContext &context, sim::ScenarioJournal &journal, bool trended) const;

    bool stratified() const noexcept;
};

} // namespace hgps::model
