#pragma once

#include "interfaces.h"
#include "modelinput.h"
#include "repository.h"
#include "runtime_context.h"

namespace hgps {

class DiseaseModule final : public UpdatableModule {
  public:
    DiseaseModule() = delete;

    DiseaseModule(std::map<core::Identifier, std::shared_ptr<DiseaseModel>> &&models);

    SimulationModuleType type() const noexcept override;

    const std::string &name() const noexcept override;

    std::size_t size() const noexcept;

    bool contains(const core::Identifier &disease_id) const noexcept;

    std::shared_ptr<DiseaseModel> &operator[](const core::Identifier &disease_id);

    const std::shared_ptr<DiseaseModel> &operator[](const core::Identifier &disease_id) const;

    void initialise_population(RuntimeContext &context) override;

    void update_population(RuntimeContext &context) override;

    double get_excess_mortality(const core::Identifier &disease_code,
                                const Person &entity) const noexcept;

  private:
    std::map<core::Identifier, std::shared_ptr<DiseaseModel>> models_;
    std::string name_{"Disease"};
};

std::unique_ptr<DiseaseModule> build_disease_module(Repository &repository,
                                                    const ModelInput &config);

} // namespace hgps
