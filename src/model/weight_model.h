// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{weight_category,weight_model,lms_definition,lms_model}.h and .cpp.
#pragma once

#include "core/identifier.h"
#include "core/types.h"
#include "person.h"

#include <cstdint>
#include <map>
#include <string>

namespace hgps::model {

enum class WeightCategory : std::uint8_t { normal, overweight, obese };

/// @throws std::invalid_argument for a value outside the enumeration.
std::string weight_category_to_string(WeightCategory value);

/// @brief One row of the LMS childhood growth reference.
struct LmsRecord {
    double lambda{};
    double mu{};
    double sigma{};
};

using LmsDataset = std::map<unsigned int, std::map<core::Gender, LmsRecord>>;

/// @brief The LMS reference table, by age and sex.
class LmsDefinition {
  public:
    LmsDefinition() = default;

    /// @throws std::invalid_argument for an empty dataset.
    explicit LmsDefinition(LmsDataset dataset);

    bool empty() const noexcept { return table_.empty(); }
    std::size_t size() const noexcept { return table_.size(); }

    unsigned int min_age() const noexcept { return table_.empty() ? 0 : table_.cbegin()->first; }
    unsigned int max_age() const noexcept { return table_.empty() ? 0 : table_.crbegin()->first; }

    bool contains(unsigned int age, core::Gender gender) const noexcept;

    /// @throws std::out_of_range for an age or sex the table does not cover.
    const LmsRecord &at(unsigned int age, core::Gender gender) const {
        return table_.at(age).at(gender);
    }

  private:
    LmsDataset table_{};
};

/// @brief Classifies a person's weight, and adjusts a child's BMI to a category-typical value.
///
/// Children are classified by LMS z-score against the growth reference; adults by the usual BMI
/// cut-offs of 25 and 30.
///
/// The baseline wraps this in a type-erasing `WeightModel` with a `Concept`/`Model` pair so that
/// a different classifier could be substituted. Only one ever was, so this is the classifier
/// itself; a second one would reintroduce the interface, at which point it would earn its keep.
class WeightModel {
  public:
    WeightModel() = delete;

    /// @throws std::invalid_argument if the LMS table does not cover the child cut-off age, which
    ///         would leave some children unclassifiable.
    explicit WeightModel(LmsDefinition definition, unsigned int child_cutoff_age = 18);

    unsigned int child_cutoff_age() const noexcept { return child_cutoff_age_; }

    /// @brief The person's weight category, from their BMI.
    WeightCategory classify_weight(const Person &person) const;

    /// @brief The category a given BMI puts this person in.
    WeightCategory classify_weight(const Person &person, double bmi) const;

    /// @brief A child's BMI replaced by the midpoint of their category; adults unchanged.
    ///
    /// Only the BMI factor is adjusted; anything else is returned as given.
    double adjust_risk_factor_value(const Person &person, const core::Identifier &factor_key,
                                    double value) const;

  private:
    LmsDefinition definition_;
    unsigned int child_cutoff_age_{18};
    core::Identifier bmi_key_{"bmi"};
};

} // namespace hgps::model
