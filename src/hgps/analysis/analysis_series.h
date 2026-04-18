#pragma once

#include "analysis_definition.h"
#include "simulation/runtime_context.h"
#include "models/weight_model.h"

#include <string>
#include <vector>

namespace hgps::analysis {

void initialise_output_channels(RuntimeContext &context, std::vector<std::string> &channels);

void calculate_population_statistics(const AnalysisDefinition &definition,
                                     const WeightModel &weight_classifier,
                                     const DoubleAgeGenderTable &residual_disability_weight,
                                     RuntimeContext &context, DataSeries &series,
                                     const std::vector<std::string> &channels);

void calculate_standard_deviation(const AnalysisDefinition &definition,
                                  const DoubleAgeGenderTable &residual_disability_weight,
                                  RuntimeContext &context, DataSeries &series);

} // namespace hgps::analysis