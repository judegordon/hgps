#include "simulation_module.h"

#include "analysis_module.h"
#include "data/demographic.h"
#include "disease/disease.h"
#include "disease/disease_table.h"
#include "data/riskfactor.h"
#include "models/ses_noise_module.h"

namespace hgps {

SimulationModuleFactory get_default_simulation_module_factory(Repository &manager) {
    auto factory = SimulationModuleFactory(manager);

    factory.register_builder(
        SimulationModuleType::SES,
        [](Repository &r, const ModelInput &m) {
            return std::shared_ptr<SimulationModule>(
                std::move(build_ses_noise_module(r, m)));
        });

    factory.register_builder(
        SimulationModuleType::Demographic,
        [](Repository &r, const ModelInput &m) {
            return std::shared_ptr<SimulationModule>(
                std::move(build_population_module(r, m)));
        });

    factory.register_builder(
        SimulationModuleType::RiskFactor,
        [](Repository &r, const ModelInput &m) {
            return std::shared_ptr<SimulationModule>(
                std::move(build_risk_factor_module(r, m)));
        });

    factory.register_builder(
        SimulationModuleType::Disease,
        [](Repository &r, const ModelInput &m) {
            return std::shared_ptr<SimulationModule>(
                std::move(build_disease_module(r, m)));
        });

    factory.register_builder(
        SimulationModuleType::Analysis,
        [](Repository &r, const ModelInput &m) {
            return std::shared_ptr<SimulationModule>(
                std::move(build_analysis_module(r, m)));
        });

    return factory;
}

} // namespace hgps