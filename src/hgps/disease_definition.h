#pragma once

#include "hgps_core/poco.h"
#include "hgps_input/data/pif_data.h"
#include "disease_table.h"
#include "gender_value.h"
#include "relative_risk.h"

namespace hgps {

using ParameterLookup = const std::map<int, DoubleGenderValue>;

struct DiseaseParameter final {
    DiseaseParameter() = default;

    DiseaseParameter(int data_time, const ParameterLookup &prevalence,
                     const ParameterLookup &survival, const ParameterLookup &deaths)
        : time_year{data_time}, prevalence_distribution{prevalence}, survival_rate{survival},
          death_weight{deaths}, max_time_since_onset{prevalence.rbegin()->first + 1} {}

    int time_year{};

    ParameterLookup prevalence_distribution{};

    ParameterLookup survival_rate{};

    ParameterLookup death_weight{};

    int max_time_since_onset{};
};

class DiseaseDefinition final {
  public:
    DiseaseDefinition(DiseaseTable &&measures_table, RelativeRiskTableMap &&diseases,
                      RelativeRiskLookupMap &&risk_factors,
                      hgps::input::PIFData &&pif_data = hgps::input::PIFData{})
        : measures_table_{std::move(measures_table)}, relative_risk_diseases_{std::move(diseases)},
          relative_risk_factors_{std::move(risk_factors)}, pif_data_{std::move(pif_data)} {}

    DiseaseDefinition(DiseaseTable &&measures_table, RelativeRiskTableMap &&diseases,
                      RelativeRiskLookupMap &&risk_factors, DiseaseParameter &&parameter,
                      hgps::input::PIFData &&pif_data = hgps::input::PIFData{})
        : measures_table_{std::move(measures_table)}, relative_risk_diseases_{std::move(diseases)},
          relative_risk_factors_{std::move(risk_factors)}, parameters_{std::move(parameter)},
          pif_data_{std::move(pif_data)} {}

    const core::DiseaseInfo &identifier() const noexcept { return measures_table_.info(); }

    const DiseaseTable &table() const noexcept { return measures_table_; }

    const RelativeRiskTableMap &relative_risk_diseases() const noexcept {
        return relative_risk_diseases_;
    }

    const RelativeRiskLookupMap &relative_risk_factors() const noexcept {
        return relative_risk_factors_;
    }

    const DiseaseParameter &parameters() const noexcept { return parameters_; }

    const hgps::input::PIFData &pif_data() const noexcept { return pif_data_; }

    void set_pif_data(hgps::input::PIFData &&pif_data) { pif_data_ = std::move(pif_data); }

    bool has_pif_data() const noexcept { return pif_data_.has_data(); }

  private:
    DiseaseTable measures_table_;
    RelativeRiskTableMap relative_risk_diseases_;
    RelativeRiskLookupMap relative_risk_factors_;
    DiseaseParameter parameters_;
    hgps::input::PIFData pif_data_;
};
} // namespace hgps
