// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{disease_table,relative_risk,disease_definition}.h and their .cpp files.
#pragma once

#include "core/array2d.h"
#include "core/entities.h"
#include "core/identifier.h"
#include "core/types.h"
#include "model/containers.h"
#include "pif_table.h"

#include <map>
#include <optional>
#include <string>

namespace hgps::model {

/// @brief The measure names the disease data uses.
struct MeasureKey final {
    static inline const std::string prevalence{"prevalence"};
    static inline const std::string mortality{"mortality"};
    static inline const std::string remission{"remission"};
    static inline const std::string incidence{"incidence"};
};

/// @brief One (age, sex) cell of a disease's measure table, keyed by measure id.
class DiseaseMeasure {
  public:
    DiseaseMeasure() = default;
    explicit DiseaseMeasure(std::map<int, double> measures) : measures_{std::move(measures)} {}

    std::size_t size() const noexcept { return measures_.size(); }

    /// @throws std::out_of_range for a measure this cell does not carry.
    double at(int measure_id) const { return measures_.at(measure_id); }
    double operator[](int measure_id) const { return measures_.at(measure_id); }

    bool contains(int measure_id) const noexcept { return measures_.contains(measure_id); }

  private:
    std::map<int, double> measures_;
};

/// @brief A disease's measures by age and sex.
class DiseaseTable {
  public:
    DiseaseTable() = delete;

    /// @throws std::invalid_argument for an empty disease code, or rows with differing column
    ///         counts — which would mean one sex is missing for some ages.
    DiseaseTable(core::DiseaseInfo info, std::map<std::string, int> measures,
                 std::map<int, std::map<core::Gender, DiseaseMeasure>> data);

    const core::DiseaseInfo &info() const noexcept { return info_; }

    std::size_t size() const noexcept { return rows() * columns(); }
    std::size_t rows() const noexcept { return data_.size(); }
    std::size_t columns() const noexcept;

    bool contains(int age) const noexcept { return data_.contains(age); }

    const std::map<std::string, int> &measures() const noexcept { return measures_; }

    /// @throws std::out_of_range for an unknown measure name.
    int at(const std::string &measure) const { return measures_.at(measure); }
    int operator[](const std::string &measure) const { return measures_.at(measure); }

    /// @throws std::out_of_range for an age or sex the table does not cover.
    DiseaseMeasure &operator()(int age, core::Gender gender) { return data_.at(age).at(gender); }
    const DiseaseMeasure &operator()(int age, core::Gender gender) const {
        return data_.at(age).at(gender);
    }

    /// @brief The lowest and highest age in the table.
    int min_age() const noexcept { return data_.empty() ? 0 : data_.begin()->first; }
    int max_age() const noexcept { return data_.empty() ? 0 : data_.rbegin()->first; }

  private:
    core::DiseaseInfo info_;
    std::map<std::string, int> measures_;
    std::map<int, std::map<core::Gender, DiseaseMeasure>> data_;
};

/// @brief A relative risk table indexed by age and a risk factor value, interpolating between the
///        value breakpoints.
class RelativeRiskLookup {
  public:
    RelativeRiskLookup() = delete;

    /// @throws std::out_of_range if the breakpoints and the value table disagree in size.
    RelativeRiskLookup(const MonotonicVector<int> &rows, const MonotonicVector<float> &columns,
                       core::FloatArray2D values);

    std::size_t size() const noexcept { return table_.size(); }
    std::size_t rows() const noexcept { return table_.rows(); }
    std::size_t columns() const noexcept { return table_.columns(); }
    bool empty() const noexcept { return rows_index_.empty() || columns_index_.empty(); }

    /// @brief The relative risk at an age for a factor value.
    ///
    /// Clamped at both ends of the value range, exact at a breakpoint, and linearly interpolated
    /// in between.
    /// @throws std::out_of_range for an age outside the table.
    float at(int age, float value) const { return lookup(age, value); }
    float operator()(int age, float value) const { return lookup(age, value); }

    bool contains(int age, float value) const noexcept {
        return rows_index_.contains(age) && columns_index_.contains(value);
    }

  private:
    core::FloatArray2D table_;
    std::map<int, std::size_t> rows_index_;
    std::map<float, std::size_t> columns_index_;

    float lookup(int age, float value) const;
};

/// @brief Disease-to-disease relative risks, by the other disease's code.
using RelativeRiskTableMap = std::map<core::Identifier, FloatAgeGenderTable>;

/// @brief Risk-factor-to-disease relative risks, by factor then sex.
using RelativeRiskLookupMap =
    std::map<core::Identifier, std::map<core::Gender, RelativeRiskLookup>>;

struct RelativeRisk {
    RelativeRiskTableMap diseases;
    RelativeRiskLookupMap risk_factors;
};

/// @brief A value per sex, as the cancer parameter tables hold it.
using DoubleGenderValue = GenderValue<double>;
using ParameterLookup = std::map<int, DoubleGenderValue>;

/// @brief The extra parameters a cancer model needs.
struct DiseaseParameter final {
    DiseaseParameter() = default;

    DiseaseParameter(int data_time, ParameterLookup prevalence, ParameterLookup survival,
                     ParameterLookup deaths);

    int time_year{};
    ParameterLookup prevalence_distribution{};
    ParameterLookup survival_rate{};
    ParameterLookup death_weight{};

    /// @brief One past the highest time-since-onset the prevalence distribution covers.
    int max_time_since_onset{};

    bool empty() const noexcept {
        return prevalence_distribution.empty() || survival_rate.empty() || death_weight.empty();
    }
};

/// @brief Everything a disease model needs about one disease, loaded once before the run.
class DiseaseDefinition final {
  public:
    DiseaseDefinition(DiseaseTable measures, RelativeRiskTableMap diseases,
                      RelativeRiskLookupMap risk_factors, DiseaseParameter parameter = {});

    const core::DiseaseInfo &identifier() const noexcept { return measures_.info(); }
    const DiseaseTable &table() const noexcept { return measures_; }
    const RelativeRiskTableMap &relative_risk_diseases() const noexcept {
        return relative_risk_diseases_;
    }
    const RelativeRiskLookupMap &relative_risk_factors() const noexcept {
        return relative_risk_factors_;
    }
    const DiseaseParameter &parameters() const noexcept { return parameters_; }

    /// @brief The population impact fraction table, when the run applies one to this disease.
    ///
    /// Optional because a PIF is a property of the *run*, not of the disease: the same definition is
    /// used with and without one ([ADR 0038](../../../docs/decisions/0038-population-impact-fraction.md)).
    const std::optional<PifTable> &population_impact_fraction() const noexcept { return pif_; }
    bool has_population_impact_fraction() const noexcept { return pif_.has_value(); }
    void set_population_impact_fraction(PifTable table) { pif_ = std::move(table); }

  private:
    DiseaseTable measures_;
    RelativeRiskTableMap relative_risk_diseases_;
    RelativeRiskLookupMap relative_risk_factors_;
    DiseaseParameter parameters_;
    std::optional<PifTable> pif_;
};

} // namespace hgps::model
