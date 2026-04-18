#include "module_factory.h"

#include <stdexcept>

namespace hgps {

SimulationModuleFactory::SimulationModuleFactory(Repository &data_repository)
    : repository_{data_repository} {}

void SimulationModuleFactory::register_builder(SimulationModuleType type,
                                               ConcreteBuilder builder) {
    builders_[type] = builder;
}

SimulationModuleFactory::ModuleType SimulationModuleFactory::create(
    SimulationModuleType type, const ModelInput &config) {
    auto it = builders_.find(type);
    if (it == builders_.end()) {
        throw std::invalid_argument{"Trying to create a module of unknown type: " +
                                    std::to_string(static_cast<int>(type))};
    }

    return it->second(repository_.get(), config);
}

} // namespace hgps