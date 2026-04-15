#pragma once

#include "types/country.h"
#include "types/population_item.h"
#include "types/mortality.h"
#include "types/disease.h"
#include "types/disease_analysis.h"
#include "types/identifier.h"
#include "types/indicator.h"
#include "types/lms_data.h"
#include "types/interval.h"

#include <optional>
#include <vector>
#include <string>
#include <functional>

namespace hgps::core {

class Datastore {
  public:
    virtual ~Datastore() = default;

    virtual std::vector<Country> get_countries() const = 0;

    virtual Country get_country(const std::string &alpha) const = 0;

    virtual std::vector<PopulationItem>
    get_population(const Country &country, std::function<bool(unsigned int)> time_filter) const = 0;

    virtual std::vector<MortalityItem>
    get_mortality(const Country &country, std::function<bool(unsigned int)> time_filter) const = 0;

    virtual std::vector<DiseaseInfo> get_diseases() const = 0;

    virtual DiseaseInfo get_disease_info(const Identifier &code) const = 0;

    virtual DiseaseEntity get_disease(const DiseaseInfo &info, const Country &country) const = 0;

    virtual std::optional<RelativeRiskEntity>
    get_relative_risk_to_disease(const DiseaseInfo &source, const DiseaseInfo &target) const = 0;

    virtual std::optional<RelativeRiskEntity>
    get_relative_risk_to_risk_factor(const DiseaseInfo &source, Gender gender,
                                     const Identifier &risk_factor_key) const = 0;

    virtual CancerParameterEntity get_disease_parameter(const DiseaseInfo &info,
                                                        const Country &country) const = 0;

    virtual DiseaseAnalysisEntity get_disease_analysis(const Country &country) const = 0;

    virtual std::vector<BirthItem>
    get_birth_indicators(const Country &country,
                         std::function<bool(unsigned int)> time_filter) const = 0;

    virtual std::vector<LmsDataRow> get_lms_parameters() const = 0;
};

} // namespace hgps::core
