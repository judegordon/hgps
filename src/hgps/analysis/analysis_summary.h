#pragma once

#include "analysis_definition.h"
#include "simulation/model_result.h"
#include "simulation/runtime_context.h"
#include "models/weight_model.h"

namespace hgps::analysis {

double calculate_disability_weight(const AnalysisDefinition &definition,
                                   const DoubleAgeGenderTable &residual_disability_weight,
                                   const Person &entity);

DALYsIndicator calculate_dalys(const AnalysisDefinition &definition,
                               const DoubleAgeGenderTable &residual_disability_weight,
                               Population &population, unsigned int max_age,
                               unsigned int death_year);

void calculate_historical_statistics(const AnalysisDefinition &definition,
                                     const DoubleAgeGenderTable &residual_disability_weight,
                                     unsigned int comorbidities, bool enable_income_analysis,
                                     RuntimeContext &context, ModelResult &result);

void classify_weight(const WeightModel &weight_classifier, DataSeries &series,
                     const Person &entity);

} // namespace hgps::analysis