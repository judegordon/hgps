#pragma once

#include "hgps_core/interfaces/datastore.h"

#include "types/disease_definition.h"
#include "types/interfaces.h"
#include "types/lms_definition.h"
#include "model_input.h"
#include "models/risk_factor_adjustable_model.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace hgps {

class Repository {
  public:
    Repository() = default;
    Repository(Repository &&) = delete;
    Repository(const Repository &) = delete;
    Repository &operator=(Repository &&) = delete;
    Repository &operator=(const Repository &) = delete;

    virtual ~Repository() = default;

    virtual core::Datastore &manager() noexcept = 0;

    virtual const RiskFactorModelDefinition &
    get_risk_factor_model_definition(const RiskFactorModelType &model_type) const = 0;

    virtual const std::vector<core::DiseaseInfo> &get_diseases() = 0;

    virtual std::optional<core::DiseaseInfo> get_disease_info(core::Identifier code) = 0;

    virtual DiseaseDefinition &get_disease_definition(const core::DiseaseInfo &info,
                                                      const ModelInput &config) = 0;

    virtual LmsDefinition &get_lms_definition() = 0;

    virtual void register_region_prevalence(
        const std::map<core::Identifier, std::map<core::Gender, std::map<std::string, double>>>
            &region_data) = 0;

    virtual void register_ethnicity_prevalence(
        const std::map<core::Identifier,
                       std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>>
            &ethnicity_data) = 0;

    virtual const std::map<core::Identifier,
                           std::map<core::Gender, std::map<std::string, double>>> &
    get_region_prevalence() const = 0;

    virtual const std::map<
        core::Identifier,
        std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>> &
    get_ethnicity_prevalence() const = 0;
};

class CachedRepository final : public Repository {
  public:
    CachedRepository() = delete;

    CachedRepository(core::Datastore &manager);

    void
    register_risk_factor_model_definition(const RiskFactorModelType &model_type,
                                          std::unique_ptr<RiskFactorModelDefinition> definition);

    core::Datastore &manager() noexcept override;

    const RiskFactorModelDefinition &
    get_risk_factor_model_definition(const RiskFactorModelType &model_type) const override;

    const std::vector<core::DiseaseInfo> &get_diseases() override;

    std::optional<core::DiseaseInfo> get_disease_info(core::Identifier code) override;

    DiseaseDefinition &get_disease_definition(const core::DiseaseInfo &info,
                                              const ModelInput &config) override;

    LmsDefinition &get_lms_definition() override;

    void register_region_prevalence(
        const std::map<core::Identifier, std::map<core::Gender, std::map<std::string, double>>>
            &region_data) override;

    void register_ethnicity_prevalence(
        const std::map<core::Identifier,
                       std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>>
            &ethnicity_data) override;

    const std::map<core::Identifier, std::map<core::Gender, std::map<std::string, double>>> &
    get_region_prevalence() const override;

    const std::map<core::Identifier,
                   std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>> &
    get_ethnicity_prevalence() const override;

    void clear_cache() noexcept;

  private:
    mutable std::mutex mutex_;
    std::reference_wrapper<core::Datastore> data_manager_;
    std::map<RiskFactorModelType, std::unique_ptr<RiskFactorModelDefinition>> rf_model_definition_;
    std::vector<core::DiseaseInfo> diseases_info_;
    std::map<core::Identifier, DiseaseDefinition> diseases_;
    LmsDefinition lms_parameters_;

    std::map<core::Identifier, std::map<core::Gender, std::map<std::string, double>>>
        region_prevalence_;

    std::map<core::Identifier,
             std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>>
        ethnicity_prevalence_;

    void load_disease_definition(const core::DiseaseInfo &info, const ModelInput &config);
};
} // namespace hgps
