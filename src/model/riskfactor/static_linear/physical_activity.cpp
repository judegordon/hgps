// Physical activity, in its two shapes.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: StaticLinearModel::{initialise_physical_activity, initialise_continuous_physical_activity,
//         initialise_simple_physical_activity} in src/HealthGPS/static_linear_model.cpp.
#include "static_linear_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kPhysicalActivity{"physicalactivity"};

} // namespace

void StaticLinearModel::initialise_physical_activity(RuntimeContext &context, Person &person,
                                                      rng::RandomSource &random) const {
    if (!parameters_->physical_activity_enabled || !person.is_active()) {
        return;
    }

    const auto &model = parameters_->physical_activity;
    double value = 0.0;

    if (model.type == "continuous") {
        // A regression on the person's own attributes, plus normal noise. This is the FINCH
        // shape, and it is the only one that gives physical activity a dependence on region,
        // ethnicity and income.
        value = evaluate_linear_model(person, model.linear, eval_options(person));
        value += random.next_normal(0.0, model.stddev);
    } else {
        // The simple shape: log-normal around the expected value for this age and sex, with the
        // −σ²/2 correction that makes the *mean* of the log-normal equal the expected value
        // rather than its median.
        const double expected_value = get_expected(context, person.gender,
                                                    static_cast<int>(person.age),
                                                    kPhysicalActivity, std::nullopt, false);
        const double draw = random.next_normal(0.0, model.stddev);
        value = expected_value * std::exp(draw - 0.5 * model.stddev * model.stddev);
    }

    // The config's range wins over the model file's own min and max, because the range is what
    // the rest of the program — the analysis, the disease models, the Kevin Hall energy balance —
    // treats as the factor's domain. The model file's bounds are the fallback.
    if (context.mapping().contains(kPhysicalActivity) &&
        context.mapping().at(kPhysicalActivity).range().has_value()) {
        value = context.mapping().at(kPhysicalActivity).get_bounded_value(value);
    } else {
        if (model.min_value.has_value()) {
            value = std::max(value, *model.min_value);
        }
        if (model.max_value.has_value()) {
            value = std::min(value, *model.max_value);
        }
    }

    person.physical_activity = value;
    person.risk_factors[kPhysicalActivity] = value;
}

} // namespace hgps::model
