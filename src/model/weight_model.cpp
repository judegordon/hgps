#include "weight_model.h"

#include "diagnostics/internal_error.h"

#include <cmath>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {

std::string weight_category_to_string(WeightCategory value) {
    switch (value) {
    case WeightCategory::normal:
        return "normal";
    case WeightCategory::overweight:
        return "overweight";
    case WeightCategory::obese:
        return "obese";
    }

    throw std::invalid_argument("Unknown weight category value");
}

LmsDefinition::LmsDefinition(LmsDataset dataset) : table_{std::move(dataset)} {
    if (table_.empty()) {
        throw std::invalid_argument("The LMS definition must not be empty.");
    }
}

bool LmsDefinition::contains(unsigned int age, core::Gender gender) const noexcept {
    const auto found = table_.find(age);
    return found != table_.end() && found->second.contains(gender);
}

WeightModel::WeightModel(LmsDefinition definition, unsigned int child_cutoff_age)
    : definition_{std::move(definition)}, child_cutoff_age_{child_cutoff_age} {
    if (definition_.min_age() > child_cutoff_age_ || definition_.max_age() < child_cutoff_age_) {
        throw std::invalid_argument(
            fmt::format("Child cut-off age outside the LMS valid range: [{}, {}]",
                        definition_.min_age(), definition_.max_age()));
    }
}

WeightCategory WeightModel::classify_weight(const Person &person) const {
    return classify_weight(person, person.get_risk_factor_value(bmi_key_));
}

WeightCategory WeightModel::classify_weight(const Person &person, double bmi) const {
    if (person.age <= child_cutoff_age_) {
        if (!definition_.contains(person.age, person.gender)) {
            throw diag::InternalError(
                fmt::format("the LMS reference has no row for age {} and {}", person.age,
                            person.gender == core::Gender::male ? "males" : "females"));
        }

        const auto &parameters = definition_.at(person.age, person.gender);

        // The LMS z-score. lambda == 0 is the log case, which is the limit of the power form.
        const double zscore =
            parameters.lambda == 0.0
                ? std::log(bmi / parameters.mu) / parameters.sigma
                : (std::pow(bmi / parameters.mu, parameters.lambda) - 1.0) /
                      (parameters.lambda * parameters.sigma);

        if (zscore > 2.0) {
            return WeightCategory::obese;
        }
        if (zscore > 1.0) {
            return WeightCategory::overweight;
        }
        return WeightCategory::normal;
    }

    if (bmi >= 30.0) {
        return WeightCategory::obese;
    }
    if (bmi >= 25.0) {
        return WeightCategory::overweight;
    }
    return WeightCategory::normal;
}

double WeightModel::adjust_risk_factor_value(const Person &person,
                                             const core::Identifier &factor_key,
                                             double value) const {
    if (factor_key != bmi_key_) {
        return value;
    }

    if (person.age > child_cutoff_age_) {
        return value;
    }

    // A child's BMI is replaced by the midpoint of their category, because the disease models'
    // relative risks are tabulated against adult-style categories.
    switch (classify_weight(person, value)) {
    case WeightCategory::normal:
        return 22.5;
    case WeightCategory::overweight:
        return 27.5;
    case WeightCategory::obese:
        return 35.0;
    }

    throw diag::InternalError("unknown weight category");
}

} // namespace hgps::model
