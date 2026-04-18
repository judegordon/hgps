#include "simulation_module.h"

#include "analysis_module.h"
#include "data/demographic.h"
#include "data/disease.h"
#include "data/disease_table.h"
#include "data/riskfactor.h"
#include "models/ses_noise_module.h"

namespace hgps {

SimulationModuleFactory get_default_simulation_module_factory(Repository &manager) {
    auto factory = SimulationModuleFactory(manager);

    factory.register_builder(SimulationModuleType::SES, build_ses_noise_module);
    factory.register_builder(SimulationModuleType::Demographic, build_population_module);
    factory.register_builder(SimulationModuleType::RiskFactor, build_risk_factor_module);
    factory.register_builder(SimulationModuleType::Disease, build_disease_module);
    factory.register_builder(SimulationModuleType::Analysis, build_analysis_module);

    return factory;
}

} // namespace hgps