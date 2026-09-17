// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{disease,default_disease_model,default_cancer_model,disease_registry}.h
//         and their .cpp files.
#pragma once

#include "core/interval.h"
#include "disease_table.h"
#include "model/containers.h"
#include "model/module.h"
#include "model/weight_model.h"

#include <map>
#include <memory>
#include <string>

namespace hgps::model {

/// @brief One disease's incidence, remission and mortality behaviour.
class DiseaseModel {
  public:
    DiseaseModel() = default;
    virtual ~DiseaseModel() = default;
    DiseaseModel(const DiseaseModel &) = delete;
    DiseaseModel &operator=(const DiseaseModel &) = delete;
    DiseaseModel(DiseaseModel &&) = delete;
    DiseaseModel &operator=(DiseaseModel &&) = delete;

    virtual core::DiseaseGroup group() const noexcept = 0;
    virtual const core::Identifier &disease_type() const noexcept = 0;

    /// @brief Gives the initial cohort the disease at its measured prevalence.
    virtual void initialise_disease_status(RuntimeContext &context) = 0;

    /// @brief Recomputes the average relative risk, once every disease's status exists.
    virtual void initialise_average_relative_risk(RuntimeContext &context) = 0;

    /// @brief One year: remission, then incidence. The order matters and is fixed.
    virtual void update_disease_status(RuntimeContext &context) = 0;

    /// @brief The excess mortality this disease adds for a person.
    virtual double get_excess_mortality(const Person &person) const = 0;
};

/// @brief The shared machinery of the two disease model families.
///
/// Both compute relative risks the same way and average them the same way; only initialisation,
/// remission and mortality differ. The baseline duplicates all of it between
/// default_disease_model.cpp and default_cancer_model.cpp — including the average-relative-risk
/// reduction, twice, in each file.
class DiseaseModelBase : public DiseaseModel {
  public:
    DiseaseModelBase(const DiseaseDefinition &definition, WeightModel classifier,
                     const core::IntegerInterval &age_range);

    const core::Identifier &disease_type() const noexcept override {
        return definition_.identifier().code;
    }

    void initialise_average_relative_risk(RuntimeContext &context) override;

    void update_disease_status(RuntimeContext &context) override;

  protected:
    const DiseaseDefinition &definition() const noexcept { return definition_; }
    const WeightModel &classifier() const noexcept { return classifier_; }

    const DoubleAgeGenderTable &average_relative_risk() const noexcept {
        return average_relative_risk_;
    }

    /// @brief The product of this person's risk-factor relative risks, in factor-name order.
    double relative_risk_for_risk_factors(const Person &person) const;

    /// @brief The product of this person's other-disease relative risks, in disease-code order.
    double relative_risk_for_diseases(const Person &person) const;

    /// @brief The population's mean relative risk by age and sex.
    ///
    /// A fixed-order reduction, so the result does not depend on the thread count. The baseline
    /// accumulates into a shared table under a mutex (audit N-7), and these averages divide into
    /// every incidence probability — so a last-bit difference here can flip a person's disease
    /// outcome.
    DoubleAgeGenderTable compute_average_relative_risk(RuntimeContext &context,
                                                       bool include_diseases) const;

    /// @brief Remission, which differs between the two families.
    virtual void update_remission_cases(RuntimeContext &context) = 0;

    /// @brief Incidence, shared: the same probability, with the family's own onset bookkeeping.
    void update_incidence_cases(RuntimeContext &context);

    /// @brief What a newly incident case looks like for this family.
    virtual Disease make_incident_case(RuntimeContext &context) = 0;

  private:
    const DiseaseDefinition &definition_;
    WeightModel classifier_;
    DoubleAgeGenderTable average_relative_risk_;
};

/// @brief A non-cancer disease: prevalence-based initialisation, then yearly remission and
///        incidence driven by relative risks.
class DefaultDiseaseModel final : public DiseaseModelBase {
  public:
    /// @throws std::invalid_argument if the definition is a cancer.
    DefaultDiseaseModel(const DiseaseDefinition &definition, WeightModel classifier,
                        const core::IntegerInterval &age_range);

    core::DiseaseGroup group() const noexcept override { return core::DiseaseGroup::other; }

    void initialise_disease_status(RuntimeContext &context) override;

    double get_excess_mortality(const Person &person) const override;

  protected:
    void update_remission_cases(RuntimeContext &context) override;
    Disease make_incident_case(RuntimeContext &context) override;
};

/// @brief A cancer: as above, plus time since onset, survival and death weights.
class DefaultCancerModel final : public DiseaseModelBase {
  public:
    /// @throws std::invalid_argument if the definition is not a cancer.
    DefaultCancerModel(const DiseaseDefinition &definition, WeightModel classifier,
                       const core::IntegerInterval &age_range);

    core::DiseaseGroup group() const noexcept override { return core::DiseaseGroup::cancer; }

    void initialise_disease_status(RuntimeContext &context) override;

    double get_excess_mortality(const Person &person) const override;

  protected:
    void update_remission_cases(RuntimeContext &context) override;
    Disease make_incident_case(RuntimeContext &context) override;

  private:
    /// @brief How long a prevalent case has already been running, drawn from the prevalence
    ///        distribution.
    int calculate_time_since_onset(RuntimeContext &context, core::Gender gender) const;
};

/// @brief Hosts one model per configured disease.
class DiseaseModule final : public UpdatableModule, public ExcessMortalityHost {
  public:
    DiseaseModule() = delete;

    explicit DiseaseModule(std::map<core::Identifier, std::unique_ptr<DiseaseModel>> models);

    ModuleType type() const noexcept override { return ModuleType::Disease; }
    const std::string &name() const noexcept override { return name_; }

    std::size_t size() const noexcept { return models_.size(); }
    bool contains(const core::Identifier &disease) const noexcept {
        return models_.contains(disease);
    }

    /// @brief Initialises status for every disease, then the averages, then one dry-run year of
    ///        incidence — in that order, because each stage needs the previous one's output.
    void initialise_population(RuntimeContext &context) override;

    void update_population(RuntimeContext &context) override;

    double excess_mortality(const core::Identifier &disease,
                            const Person &person) const override;

  private:
    // Ordered by disease code, so the diseases are visited — and their RNG draws taken — in a
    // stated order. The baseline builds this map from inside a parallel loop over the disease
    // list, which is also where its data race lives (audit B-02).
    std::map<core::Identifier, std::unique_ptr<DiseaseModel>> models_;
    std::string name_{"Disease"};
};

/// @brief Builds the model for a disease's group.
std::unique_ptr<DiseaseModel> create_disease_model(const DiseaseDefinition &definition,
                                                   WeightModel classifier,
                                                   const core::IntegerInterval &age_range);

} // namespace hgps::model
