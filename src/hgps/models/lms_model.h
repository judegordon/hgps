#pragma once

#include "types/lms_definition.h"
#include "data/person.h"

#include "hgps_core/types/identifier.h"
#include <functional>
#include <string>

namespace hgps {

class LmsModel {
  public:
    LmsModel() = delete;

    LmsModel(LmsDefinition &definition);

    unsigned int child_cutoff_age() const noexcept;

    WeightCategory classify_weight(const Person &entity) const;

    double adjust_risk_factor_value(const Person &entity, const core::Identifier &factor_key,
                                    double value) const;

  private:
    std::reference_wrapper<LmsDefinition> definition_;
    unsigned int child_cutoff_age_{18};
    core::Identifier bmi_key_{"bmi"};

    WeightCategory classify_weight_bmi(const Person &entity, double bmi) const;
};
} // namespace hgps
