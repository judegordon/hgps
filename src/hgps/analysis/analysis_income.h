#pragma once

#include "analysis_definition.h"
#include "hgps_input/config/config_types.h"
#include "simulation/model_result.h"
#include "simulation/runtime_context.h"
#include "models/weight_model.h"

#include <string>
#include <vector>

namespace hgps::analysis {

std::vector<core::Income> get_available_income_categories(RuntimeContext &context);

std::string income_category_to_string(core::Income income);

void calculate_income_based_statistics(const AnalysisDefinition &definition,
                                       const DoubleAgeGenderTable &residual_disability_weight,
                                       unsigned int comorbidities, RuntimeContext &context,
                                       ModelResult &result);

void calculate_income_based_population_statistics(
    const AnalysisDefinition &definition, const WeightModel &weight_classifier,
    const DoubleAgeGenderTable &residual_disability_weight, RuntimeContext &context,
    DataSeries &series);

void calculate_income_based_standard_deviation(
    const AnalysisDefinition &definition, const DoubleAgeGenderTable &residual_disability_weight,
    RuntimeContext &context, DataSeries &series);

} // namespace hgps::analysis