#pragma once

#include "core/entities.h"
#include "diagnostics/issue_report.h"
#include "index.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hgps::data {

/// @brief The back-end data store: a directory tree described by index.json.
///
/// Every loader takes an IssueReport and returns an optional rather than throwing, and the
/// application loads everything a run needs during start-up, before any worker thread exists.
/// That is a deliberate difference from the baseline, whose CachedRepository loads disease
/// definitions lazily from inside a parallel loop behind a "lock-free multiple readers" fast path
/// that races a concurrent insertion (audit B-02, confirmed by ThreadSanitizer). Loading up front
/// removes the race by construction rather than by locking.
class Store {
  public:
    /// @brief Opens a store: reads index.json and validates the disease registry against the tree.
    static std::optional<Store> open(const std::filesystem::path &root, diag::IssueReport &report);

    const DataIndex &index() const noexcept { return index_; }

    /// @brief Every country, ordered by name.
    std::optional<std::vector<core::Country>> countries(diag::IssueReport &report) const;

    /// @brief One country by ISO alpha-2 or alpha-3 code, matched case-insensitively.
    std::optional<core::Country> country(const std::string &alpha, diag::IssueReport &report) const;

    /// @brief Every registered disease, ordered by name.
    std::vector<core::DiseaseInfo> diseases() const;

    /// @brief One disease's registry information.
    std::optional<core::DiseaseInfo> disease_info(const core::Identifier &code,
                                                  diag::IssueReport &report) const;

    using TimeFilter = std::function<bool(unsigned int)>;

    /// @brief The UN population series, ordered by (year, age).
    std::optional<std::vector<core::PopulationItem>>
    population(const core::Country &country, const TimeFilter &time_filter,
               diag::IssueReport &report) const;

    /// @brief The UN mortality series, ordered by (year, age).
    std::optional<std::vector<core::MortalityItem>>
    mortality(const core::Country &country, const TimeFilter &time_filter,
              diag::IssueReport &report) const;

    /// @brief Births and the sex ratio at birth, by year.
    std::optional<std::vector<core::BirthItem>>
    birth_indicators(const core::Country &country, const TimeFilter &time_filter,
                     diag::IssueReport &report) const;

    /// @brief Life expectancy by year.
    std::optional<std::vector<core::LifeExpectancyItem>>
    life_expectancy(const core::Country &country, diag::IssueReport &report) const;

    /// @brief One disease's measure table for a country.
    std::optional<core::DiseaseEntity> disease(const core::DiseaseInfo &info,
                                               const core::Country &country,
                                               diag::IssueReport &report) const;

    /// @brief Disease-to-disease relative risks.
    /// @return nullopt when the file is absent or holds only default values — both are ordinary,
    ///         and both are reported as warnings so the reader knows the interaction is off.
    std::optional<core::RelativeRiskEntity>
    relative_risk_to_disease(const core::DiseaseInfo &source, const core::DiseaseInfo &target,
                             diag::IssueReport &report) const;

    /// @brief Risk-factor-to-disease relative risks for one sex.
    std::optional<core::RelativeRiskEntity>
    relative_risk_to_risk_factor(const core::DiseaseInfo &source, core::Gender gender,
                                 const core::Identifier &risk_factor,
                                 diag::IssueReport &report) const;

    /// @brief The extra parameters a cancer model needs.
    std::optional<core::CancerParameterEntity>
    cancer_parameters(const core::DiseaseInfo &info, const core::Country &country,
                      diag::IssueReport &report) const;

    /// @brief Disability weights, life expectancy and cost of disease.
    std::optional<core::DiseaseAnalysisEntity> disease_analysis(const core::Country &country,
                                                                diag::IssueReport &report) const;

    /// @brief The LMS childhood growth reference.
    std::optional<std::vector<core::LmsDataRow>> lms_parameters(diag::IssueReport &report) const;

  private:
    explicit Store(DataIndex index);

    DataIndex index_;

    std::filesystem::path demographic_file(const std::string &section,
                                           const core::Country &country,
                                           diag::IssueReport &report) const;
};

} // namespace hgps::data
