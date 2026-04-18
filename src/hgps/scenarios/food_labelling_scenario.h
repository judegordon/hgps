#pragma once
#include "data/gender_value.h"
#include "intervention_scenario.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace hgps {

struct AdjustmentFactor {
    AdjustmentFactor(std::string factor_name, double value)
        : identifier{factor_name}, adjustment{value} {}

    core::Identifier identifier;
    double adjustment{};
};

struct PolicyCoverage {
    PolicyCoverage() = delete;

    PolicyCoverage(const std::vector<double> &rates, unsigned int effect_time)
        : cutoff_time{effect_time} {
        if (rates.size() != 2) {
            throw std::invalid_argument(
                "The number of transfer coefficients must be 2 (short, long) term.");
        }

        short_term_rate = rates[0];
        long_term_rate = rates[1];
    }

    double short_term_rate{};
    double long_term_rate{};
    unsigned int cutoff_time{};
};

struct TransferCoefficient {
    TransferCoefficient() = delete;

    TransferCoefficient(std::vector<double> values, unsigned int child_age)
        : child{}, adult{}, child_cutoff_age{child_age} {
        if (values.size() != 4) {
            throw std::out_of_range("The number of transfer coefficients must be 4.");
        }

        child.males = values[0];
        child.females = values[1];
        adult.males = values[2];
        adult.females = values[3];
    }

    DoubleGenderValue child;
    DoubleGenderValue adult;
    unsigned int child_cutoff_age{};

    double get_value(core::Gender gender, unsigned int age) const noexcept {
        if (gender == core::Gender::male) {
            return age <= child_cutoff_age ? child.males : adult.males;
        }
        return age <= child_cutoff_age ? child.females : adult.females;
    }
};

struct FoodLabellingDefinition {
    PolicyInterval active_period;
    std::vector<PolicyImpact> impacts;
    AdjustmentFactor adjustment_risk_factor;
    PolicyCoverage coverage;
    TransferCoefficient transfer_coefficient;
};

class FoodLabellingScenario final : public InterventionScenario {
  public:
    FoodLabellingScenario() = delete;

    explicit FoodLabellingScenario(FoodLabellingDefinition &&definition);

    void clear() noexcept override;

    double apply(Random &generator, Person &entity, int time,
                 const core::Identifier &risk_factor_key, double value) override;

    const PolicyInterval &active_period() const noexcept override;
    const std::vector<PolicyImpact> &impacts() const noexcept override;

  private:
    static std::size_t make_book_key(std::size_t entity_id,
                                     const core::Identifier &risk_factor_key) noexcept;

    const PolicyImpact *find_active_impact(const core::Identifier &risk_factor_key,
                                           unsigned int age) const noexcept;

    double calculate_policy_impact(const Person &entity,
                                   const PolicyImpact &impact) const noexcept;

    FoodLabellingDefinition definition_;
    std::unordered_map<std::size_t, int> interventions_book_{};
};

} // namespace hgps