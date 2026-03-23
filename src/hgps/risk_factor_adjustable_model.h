#pragma once

#include "hgps_core/forward_type.h"
#include "hgps_core/identifier.h"

#include "map2d.h"
#include "risk_factor_model.h"
#include "runtime_context.h"

#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

namespace { // anonymous namespace

using OptionalRange = std::optional<std::reference_wrapper<const hgps::core::DoubleInterval>>;

using OptionalRanges =
    std::optional<std::reference_wrapper<const std::vector<hgps::core::DoubleInterval>>>;

} // anonymous namespace

namespace hgps {

enum class TrendType {
    Null,       ///< No trends applied to factors mean adjustment
    UPFTrend,   ///< UPF (ultra-processed food) trends; config: "upf_trend", "trend" or "UPFTrend"
    IncomeTrend ///< Income-based trends applied to factors mean adjustment
};

inline bool operator==(TrendType lhs, TrendType rhs) noexcept {
    return static_cast<int>(lhs) == static_cast<int>(rhs);
}

inline bool operator!=(TrendType lhs, TrendType rhs) noexcept { return !(lhs == rhs); }

using RiskFactorSexAgeTable = UnorderedMap2d<core::Gender, core::Identifier, std::vector<double>>;

class RiskFactorAdjustableModel : public RiskFactorModel {
  public:
    RiskFactorAdjustableModel(
        std::shared_ptr<RiskFactorSexAgeTable> expected,
        std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
        std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
        TrendType trend_type = TrendType::Null,
        std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend =
            nullptr,
        std::shared_ptr<std::unordered_map<core::Identifier, double>>
            expected_income_trend_decay_factors = nullptr);

    virtual double get_expected(RuntimeContext &context, core::Gender sex, int age,
                                const core::Identifier &factor, OptionalRange range,
                                bool apply_trend) const;

    void adjust_risk_factors(RuntimeContext &context, const std::vector<core::Identifier> &factors,
                             OptionalRanges ranges, bool apply_trend) const;

    int get_trend_steps(const core::Identifier &factor) const;

    const std::shared_ptr<std::unordered_map<core::Identifier, double>> &
    get_expected_trend() const noexcept {
        return expected_trend_;
    }

    void set_logistic_factors(const std::unordered_set<core::Identifier> &logistic_factors);

  private:
    RiskFactorSexAgeTable calculate_adjustments(RuntimeContext &context,
                                                const std::vector<core::Identifier> &factors,
                                                OptionalRanges ranges, bool apply_trend) const;

    static RiskFactorSexAgeTable
    calculate_simulated_mean(Population &population, core::IntegerInterval age_range,
                             const std::vector<core::Identifier> &factors,
                             const std::unordered_set<core::Identifier> &logistic_factors = {});

    std::shared_ptr<RiskFactorSexAgeTable> expected_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend_;
    std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps_;

    // Trend type for factors mean adjustment
    TrendType trend_type_;

    // Income trend data structures (only used when trend_type_ == TrendType::IncomeTrend)
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_income_trend_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>>
        expected_income_trend_decay_factors_;

    // Logistic factors for simulated mean calculation (factors that use 2-stage modeling)
    std::unordered_set<core::Identifier> logistic_factors_;
};

class RiskFactorAdjustableModelDefinition : public RiskFactorModelDefinition {
  public:
    ~RiskFactorAdjustableModelDefinition() override = default;

    RiskFactorAdjustableModelDefinition(
        std::unique_ptr<RiskFactorSexAgeTable> expected,
        std::unique_ptr<std::unordered_map<core::Identifier, double>> expected_trend,
        std::unique_ptr<std::unordered_map<core::Identifier, int>> trend_steps,
        TrendType trend_type = TrendType::Null);

  protected:
    std::shared_ptr<RiskFactorSexAgeTable> expected_;
    std::shared_ptr<std::unordered_map<core::Identifier, double>> expected_trend_;
    std::shared_ptr<std::unordered_map<core::Identifier, int>> trend_steps_;
    TrendType trend_type_;
};

} // namespace hgps
