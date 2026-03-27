#pragma once

#include "interfaces.h"
#include "mapping.h"
#include "risk_factor_adjustable_model.h"

#include <Eigen/Dense>
#include <unordered_map>
#include <vector>

namespace hgps {

struct LinearModelParams {
    double intercept{};
    std::unordered_map<core::Identifier, double> coefficients{};
    std::unordered_map<core::Identifier, double> log_coefficients{};
};

struct PhysicalActivityModel {
    std::string model_type{"simple"};

    double intercept{};

    std::unordered_map<core::Identifier, double> coefficients;

    double min_value = 0.0;

    double max_value = std::numeric_limits<double>::max();

    double stddev = 0.06;
};

class StaticLinearModel final : public RiskFactorAdjustableModel {
  public:
    StaticLinearModel(
        std::shared_ptr<RiskFactorSexAgeTable> expected,
        std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
        std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
        std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend_boxcox,
        const std::vector<core::Identifier> &names, const std::vector<LinearModelParams> &models,
        const std::vector<core::DoubleInterval> &ranges, const std::vector<double> &lambda,
        const std::vector<double> &stddev, const Eigen::MatrixXd &cholesky,
        const std::vector<LinearModelParams> &policy_models,
        const std::vector<core::DoubleInterval> &policy_ranges,
        const Eigen::MatrixXd &policy_cholesky,
        std::shared_ptr<std::vector<LinearModelParams>> trend_models,
        std::shared_ptr<std::vector<core::DoubleInterval>> trend_ranges,
        std::shared_ptr<std::vector<double>> trend_lambda, double info_speed,
        const std::unordered_map<core::Identifier, std::unordered_map<core::Gender, double>>
            &rural_prevalence,
        const std::unordered_map<core::Income, LinearModelParams> &income_models,
        double physical_activity_stddev, TrendType trend_type,
        std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend =
            nullptr,
        std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_boxcox =
            nullptr,
        std::shared_ptr<std::unordered_map<core::Identifier, int>> income_trend_steps = nullptr,
        std::shared_ptr<std::vector<LinearModelParams>> income_trend_models = nullptr,
        std::shared_ptr<std::vector<core::DoubleInterval>> income_trend_ranges = nullptr,
        std::shared_ptr<std::vector<double>> income_trend_lambda = nullptr,
        std::shared_ptr<std::unordered_map<core::Identifier, double>> income_trend_decay_factors =
            nullptr,
        bool is_continuous_income_model = false,
        const LinearModelParams &continuous_income_model = LinearModelParams{},
        int income_categories = 3, // or 4
        const std::unordered_map<core::Identifier, PhysicalActivityModel>
            &physical_activity_models = {},
        bool has_active_policies = true,
        const std::vector<LinearModelParams> &logistic_models = {});

    RiskFactorModelType type() const noexcept override;

    std::string name() const noexcept override;

    bool is_continuous_income_model() const noexcept;

    void generate_risk_factors(RuntimeContext &context) override;

    void update_risk_factors(RuntimeContext &context) override;

  private:
    static double inverse_box_cox(double factor, double lambda);

    void initialise_factors(RuntimeContext &context, Person &person, Random &random) const;

    void update_factors(RuntimeContext &context, Person &person, Random &random) const;

    void initialise_UPF_trends(RuntimeContext &context, Person &person) const;

    void update_UPF_trends(RuntimeContext &context, Person &person) const;

    void initialise_income(RuntimeContext &context, Person &person, Random &random);

    void update_income(RuntimeContext &context, Person &person, Random &random);

    void update_income_trends(RuntimeContext &context, Person &person) const;

    void initialise_income_trends(RuntimeContext &context, Person &person) const;

    void initialise_policies(RuntimeContext &context, Person &person, Random &random,
                             bool intervene) const;

    void update_policies(RuntimeContext &context, Person &person, bool intervene) const;

    void apply_policies(Person &person, bool intervene) const;

    std::vector<double> compute_linear_models(RuntimeContext &context, Person &person,
                                              const std::vector<LinearModelParams> &models) const;

    std::vector<double> compute_residuals(Random &random, const Eigen::MatrixXd &cholesky) const;

    double calculate_zero_probability(Person &person, size_t risk_factor_index) const;

    void initialise_sector(Person &person, Random &random) const;

    void update_sector(Person &person, Random &random) const;

    // Continuous income model support (FINCH approach)
    bool is_continuous_income_model_;
    LinearModelParams continuous_income_model_;
    int income_categories_;

    double calculate_continuous_income(Person &person, Random &random);

    core::Income convert_income_continuous_to_category(double continuous_income,
                                                       const Population &population,
                                                       Random &random) const;

    core::Income convert_income_to_category(double continuous_income,
                                            const std::vector<double> &quartile_thresholds) const;

    static std::vector<double> calculate_income_quartiles(const Population &population);

    static std::vector<double> calculate_income_tertiles(const Population &population);

    void initialise_categorical_income(Person &person, Random &random);

    void initialise_continuous_income(RuntimeContext &context, Person &person, Random &random);

    void initialise_physical_activity(RuntimeContext &context, Person &person,
                                      Random &random) const;

    std::pair<std::vector<core::Identifier>, std::vector<core::DoubleInterval>>
    build_extended_factors_list(RuntimeContext &context,
                                const std::vector<core::Identifier> &base_factors,
                                const std::vector<core::DoubleInterval> &base_ranges,
                                bool for_trended_adjustment = false) const;

    static void initialise_continuous_physical_activity(RuntimeContext &context, Person &person,
                                                        Random &random,
                                                        const PhysicalActivityModel &model);

    void initialise_simple_physical_activity(RuntimeContext &context, Person &person,
                                             Random &random,
                                             const PhysicalActivityModel &model) const;

    // Regular trend member variables
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend_boxcox_;
    std::shared_ptr<std::vector<LinearModelParams>> trend_models_;
    std::shared_ptr<std::vector<core::DoubleInterval>> trend_ranges_;
    std::shared_ptr<std::vector<double>> trend_lambda_;

    // Income trend member variables
    TrendType trend_type_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_boxcox_;
    std::shared_ptr<std::unordered_map<core::Identifier, int>> income_trend_steps_;
    std::shared_ptr<std::vector<LinearModelParams>> income_trend_models_;
    std::shared_ptr<std::vector<core::DoubleInterval>> income_trend_ranges_;
    std::shared_ptr<std::vector<double>> income_trend_lambda_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> income_trend_decay_factors_;

    // Common member variables
    const std::vector<core::Identifier> &names_;
    const std::vector<LinearModelParams> &models_;
    const std::vector<core::DoubleInterval> &ranges_;
    const std::vector<double> &lambda_;
    const std::vector<double> &stddev_;
    const Eigen::MatrixXd &cholesky_;
    const std::vector<LinearModelParams> &policy_models_;
    const std::vector<core::DoubleInterval> &policy_ranges_;
    const Eigen::MatrixXd &policy_cholesky_;
    const double info_speed_;
    const std::unordered_map<core::Identifier, std::unordered_map<core::Gender, double>>
        &rural_prevalence_;
    const std::unordered_map<core::Income, LinearModelParams> &income_models_;
    const double physical_activity_stddev_;

    // Physical activity model support (both India and FINCH approaches)
    std::unordered_map<core::Identifier, PhysicalActivityModel> physical_activity_models_;
    bool has_physical_activity_models_ = false;
    // Policy optimization flag - Mahima's enhancement
    bool has_active_policies_;

    // Two-stage modeling: Logistic regression for zero probability (optional)
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members) reference to model data
    const std::vector<LinearModelParams> &logistic_models_;
};

class StaticLinearModelDefinition : public RiskFactorAdjustableModelDefinition {
  public:
    StaticLinearModelDefinition(
        std::unique_ptr<RiskFactorSexAgeTable> expected,
        std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
        std::unique_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
        std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend_boxcox,
        std::vector<core::Identifier> names, std::vector<LinearModelParams> models,
        std::vector<core::DoubleInterval> ranges, std::vector<double> lambda,
        std::vector<double> stddev, Eigen::MatrixXd cholesky,
        std::vector<LinearModelParams> policy_models,
        std::vector<core::DoubleInterval> policy_ranges, Eigen::MatrixXd policy_cholesky,
        std::unique_ptr<std::vector<LinearModelParams>> trend_models,
        std::unique_ptr<std::vector<core::DoubleInterval>> trend_ranges,
        std::unique_ptr<std::vector<double>> trend_lambda, double info_speed,
        std::unordered_map<core::Identifier, std::unordered_map<core::Gender, double>>
            rural_prevalence,
        std::unordered_map<core::Income, LinearModelParams> income_models,
        double physical_activity_stddev, TrendType trend_type = TrendType::Null,
        std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend =
            nullptr,
        std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_boxcox =
            nullptr,
        std::unique_ptr<std::unordered_map<core::Identifier, int>> income_trend_steps = nullptr,
        std::unique_ptr<std::vector<LinearModelParams>> income_trend_models = nullptr,
        std::unique_ptr<std::vector<core::DoubleInterval>> income_trend_ranges = nullptr,
        std::unique_ptr<std::vector<double>> income_trend_lambda = nullptr,
        std::unique_ptr<std::unordered_map<core::Identifier, double>> income_trend_decay_factors =
            nullptr,
        bool is_continuous_income_model = false,
        const LinearModelParams &continuous_income_model = LinearModelParams{},
        int income_categories = 3, // or 4
        const std::unordered_map<core::Identifier, PhysicalActivityModel>
            &physical_activity_models = {},
        bool has_active_policies = true,
        std::vector<LinearModelParams> logistic_models = {});

    std::unique_ptr<RiskFactorModel> create_model() const override;

  private:
    // Regular trend member variables
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend_boxcox_;
    std::shared_ptr<std::vector<LinearModelParams>> trend_models_;
    std::shared_ptr<std::vector<core::DoubleInterval>> trend_ranges_;
    std::shared_ptr<std::vector<double>> trend_lambda_;

    // Income trend member variables
    TrendType trend_type_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_boxcox_;
    std::shared_ptr<std::unordered_map<core::Identifier, int>> income_trend_steps_;
    std::shared_ptr<std::vector<LinearModelParams>> income_trend_models_;
    std::shared_ptr<std::vector<core::DoubleInterval>> income_trend_ranges_;
    std::shared_ptr<std::vector<double>> income_trend_lambda_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> income_trend_decay_factors_;

    // Common member variables
    std::vector<core::Identifier> names_;
    std::vector<LinearModelParams> models_;
    std::vector<core::DoubleInterval> ranges_;
    std::vector<double> lambda_;
    std::vector<double> stddev_;
    Eigen::MatrixXd cholesky_;
    std::vector<LinearModelParams> policy_models_;
    std::vector<core::DoubleInterval> policy_ranges_;
    Eigen::MatrixXd policy_cholesky_;
    double info_speed_;
    std::unordered_map<core::Identifier, std::unordered_map<core::Gender, double>>
        rural_prevalence_;
    std::unordered_map<core::Income, LinearModelParams> income_models_;
    double physical_activity_stddev_;

    // Physical activity model support (FINCH approach)
    std::unordered_map<core::Identifier, PhysicalActivityModel> physical_activity_models_;
    bool has_physical_activity_models_ = false;

    // Two-stage modeling: Logistic regression for zero probability (optional)
    std::vector<LinearModelParams> logistic_models_;

    // Continuous income model support (FINCH approach)
    bool is_continuous_income_model_;
    LinearModelParams continuous_income_model_;
    int income_categories_;
    // Policy optimization flag - Mahima
    bool has_active_policies_;
};

} // namespace hgps
