#pragma once
#include "data/data_series.h"
#include "data/gender_value.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hgps {

struct ResultByGender {
    double male{};
    double female{};
};

struct ResultByIncome {
    double low{};
    double lowermiddle{};
    double middle{};
    double uppermiddle{};
    double high{};
};

struct ResultByIncomeGender {
    ResultByGender low{};
    ResultByGender lowermiddle{};
    ResultByGender middle{};
    ResultByGender uppermiddle{};
    ResultByGender high{};
};

struct DALYsIndicator {
    double years_of_life_lost{};
    double years_lived_with_disability{};
    double disability_adjusted_life_years{};
};

struct ModelResult {
    ModelResult() = delete;
    explicit ModelResult(unsigned int sample_size);

    int population_size{};
    IntegerGenderValue number_alive{};
    int number_dead{};
    int number_emigrated{};
    ResultByGender average_age{};
    DALYsIndicator indicators{};

    std::map<std::string, ResultByGender> risk_factor_average{};
    std::map<std::string, ResultByGender> disease_prevalence{};
    std::map<unsigned int, ResultByGender> comorbidity{};
    std::map<std::string, double> metrics{};

    DataSeries series;

    std::optional<ResultByIncome> population_by_income{};
    std::optional<std::map<std::string, ResultByIncomeGender>> risk_factor_average_by_income{};
    std::optional<std::map<std::string, ResultByIncomeGender>> disease_prevalence_by_income{};
    std::optional<std::map<unsigned int, ResultByIncomeGender>> comorbidity_by_income{};

    int number_of_recyclable() const noexcept;
};

} // namespace hgps