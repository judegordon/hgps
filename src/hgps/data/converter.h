#pragma once

#include "analysis_definition.h"
#include "disease_definition.h"
#include "disease_table.h"
#include "gender_table.h"
#include "life_table.h"
#include "lms_definition.h"
#include "model_input.h"
#include "relative_risk.h"

#include "hgps_core/interfaces/datastore.h"

#include "hgps_core/types/disease.h"
#include "hgps_core/types/disease_analysis.h"
#include "hgps_core/types/identifier.h"
#include "hgps_core/types/life_expectancy.h"
#include "hgps_core/types/lms_data.h"
#include "hgps_core/types/mortality.h"

namespace hgps::detail {

class StoreConverter {
  public:
    StoreConverter() = delete;

    static core::Gender to_gender(const std::string &name);
    static DiseaseTable to_disease_table(const core::DiseaseEntity &entity);
    static FloatAgeGenderTable to_relative_risk_table(const core::RelativeRiskEntity &entity);
    static RelativeRiskLookup to_relative_risk_lookup(const core::RelativeRiskEntity &entity);
    static AnalysisDefinition to_analysis_definition(const core::DiseaseAnalysisEntity &entity);
    static LifeTable to_life_table(const std::vector<core::BirthItem> &births,
                                   const std::vector<core::MortalityItem> &deaths);
    static DiseaseParameter to_disease_parameter(const core::CancerParameterEntity &entity);
    static LmsDefinition to_lms_definition(const std::vector<core::LmsDataRow> &dataset);
};

RelativeRisk create_relative_risk(const RelativeRiskInfo &info);

} // namespace hgps::detail