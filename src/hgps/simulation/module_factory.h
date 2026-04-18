#pragma once

#include "types/interfaces.h"
#include "data/model_input.h"
#include "data/repository.h"

#include <functional>
#include <unordered_map>

namespace hgps {

class SimulationModuleFactory {
  public:
    using ModuleType = std::shared_ptr<SimulationModule>;
    using ConcreteBuilder = ModuleType (*)(Repository &, const ModelInput &);

    SimulationModuleFactory() = delete;
    explicit SimulationModuleFactory(Repository &data_repository);

    void register_builder(SimulationModuleType type, ConcreteBuilder builder);
    ModuleType create(SimulationModuleType type, const ModelInput &config);

  private:
    std::reference_wrapper<Repository> repository_;
    std::unordered_map<SimulationModuleType, ConcreteBuilder> builders_;
};

} // namespace hgps