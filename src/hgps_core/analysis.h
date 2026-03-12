#pragma once
#include "forward_type.h"
#include <map>
#include <string>
#include <vector>

namespace hgps::core {

struct LifeExpectancyItem {
    int at_time{};

    float both{};

    float male{};

    float female{};
};

struct DiseaseAnalysisEntity {
    std::map<std::string, float> disability_weights{};

    std::vector<LifeExpectancyItem> life_expectancy{};

    std::map<int, std::map<Gender, double>> cost_of_diseases{};

    bool empty() const noexcept { return cost_of_diseases.empty() || life_expectancy.empty(); }
};

struct LmsDataRow {
    int age{};

    Gender gender{};

    double lambda{};

    double mu{};

    double sigma{};
};
} // namespace hgps::core
