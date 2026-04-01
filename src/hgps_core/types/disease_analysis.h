#pragma once
#include "forward_type.h"
#include "life_expectancy.h"
#include <map>
#include <string>
#include <vector>

namespace hgps::core {

struct DiseaseAnalysisEntity {
    std::map<std::string, float> disability_weights{};

    std::vector<LifeExpectancyItem> life_expectancy{};

    std::map<int, std::map<Gender, double>> cost_of_diseases{};

    bool empty() const noexcept { return cost_of_diseases.empty() || life_expectancy.empty(); }
};

} // namespace hgps::core