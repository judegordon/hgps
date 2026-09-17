// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/{country,disease,analysis,mortality,indicator,population_item}.h.
#pragma once

#include "identifier.h"
#include "types.h"

#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace hgps::core {

/// @brief An ISO 3166-1 country.
struct Country {
    int code{};
    std::string name{};
    std::string alpha2{};
    std::string alpha3{};

    auto operator<=>(const Country &rhs) const { return name <=> rhs.name; }
    bool operator==(const Country &rhs) const = default;
};

/// @brief A row of the UN population series: people by age, sex and year.
struct PopulationItem {
    int location_id{};
    int at_time{};
    int with_age{};
    float males{};
    float females{};
    float total{};
};

inline bool operator<(const PopulationItem &lhs, const PopulationItem &rhs) {
    return std::tie(lhs.at_time, lhs.with_age) < std::tie(rhs.at_time, rhs.with_age);
}

inline bool operator>(const PopulationItem &lhs, const PopulationItem &rhs) {
    return std::tie(lhs.at_time, lhs.with_age) > std::tie(rhs.at_time, rhs.with_age);
}

/// @brief A row of the UN mortality series: deaths by age, sex and year.
struct MortalityItem {
    int location_id{};
    int at_time{};
    int with_age{};
    float males{};
    float females{};
    float total{};
};

inline bool operator<(const MortalityItem &lhs, const MortalityItem &rhs) {
    return std::tie(lhs.at_time, lhs.with_age) < std::tie(rhs.at_time, rhs.with_age);
}

inline bool operator>(const MortalityItem &lhs, const MortalityItem &rhs) {
    return std::tie(lhs.at_time, lhs.with_age) > std::tie(rhs.at_time, rhs.with_age);
}

/// @brief Births in a year, with the sex ratio at birth (males per 100 female births).
struct BirthItem {
    int at_time{};
    float number{};
    float sex_ratio{};
};

/// @brief Life expectancy in a year.
struct LifeExpectancyItem {
    int at_time{};
    float both{};
    float male{};
    float female{};
};

struct DiseaseInfo {
    DiseaseGroup group{};
    Identifier code{};
    std::string name{};
};

inline bool operator<(const DiseaseInfo &lhs, const DiseaseInfo &rhs) {
    return lhs.name < rhs.name;
}

inline bool operator>(const DiseaseInfo &lhs, const DiseaseInfo &rhs) {
    return lhs.name > rhs.name;
}

/// @brief One (age, sex) row of a disease's measures, keyed by measure index.
struct DiseaseItem {
    int with_age{};
    Gender gender{};
    std::map<int, double> measures{};
};

/// @brief A disease's full measure table for one country.
struct DiseaseEntity {
    Country country{};
    DiseaseInfo info{};
    std::map<std::string, int> measures{};
    std::vector<DiseaseItem> items{};

    bool empty() const noexcept { return measures.empty() || items.empty(); }
};

/// @brief A relative-risk table: named columns and rows of values.
struct RelativeRiskEntity {
    std::vector<std::string> columns{};
    std::vector<std::vector<float>> rows{};

    bool empty() const noexcept { return rows.empty(); }
};

/// @brief The extra per-country parameters a cancer model needs.
struct CancerParameterEntity {
    int at_time{};
    std::vector<LookupGenderValue> death_weight{};
    std::vector<LookupGenderValue> prevalence_distribution{};
    std::vector<LookupGenderValue> survival_rate{};

    bool empty() const noexcept {
        return death_weight.empty() || prevalence_distribution.empty() || survival_rate.empty();
    }
};

/// @brief The burden-of-disease inputs: disability weights, life expectancy, cost of disease.
struct DiseaseAnalysisEntity {
    std::map<std::string, float> disability_weights{};
    std::vector<LifeExpectancyItem> life_expectancy{};
    std::map<int, std::map<Gender, double>> cost_of_diseases{};

    bool empty() const noexcept { return cost_of_diseases.empty() || life_expectancy.empty(); }
};

/// @brief One row of the LMS (lambda-mu-sigma) childhood growth reference.
struct LmsDataRow {
    int age{};
    Gender gender{};
    double lambda{};
    double mu{};
    double sigma{};
};

} // namespace hgps::core
