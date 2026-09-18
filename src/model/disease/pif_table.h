// The population impact fraction: a per-disease multiplier on incidence in the intervention
// scenario.
//
// A PIF is the share of a disease's incidence attributable to a risk factor that a policy removes,
// tabulated by sex, age and years since the intervention started. The intervention scenario's
// incidence probability is multiplied by (1 − PIF); the baseline scenario's is not. That is upstream's
// mechanism (`default_disease_model.cpp:283`, `default_cancer_model.cpp:295`) and this is the same
// arithmetic with the table read and validated differently
// (docs/decisions/0038-population-impact-fraction.md).
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Input/pif_data.{h,cpp}.
#pragma once

#include "core/entities.h"
#include "core/types.h"
#include "diagnostics/issue_report.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hgps::model {

/// @brief One PIF table: a dense (year since intervention, sex, age) lookup.
///
/// Dense and complete, both checked at load. Upstream sizes its array from the observed minimum and
/// maximum of each column and leaves every cell the file did not mention default-constructed — so a
/// file missing a row reads as a PIF of exactly zero for that person, silently, and a file missing
/// half its rows produces a policy that works for half the population. Here a gap is a located error
/// naming the first missing combination.
class PifTable {
  public:
    PifTable() = default;

    /// @brief Builds the table from the rows of an `IF{COUNTRY_CODE}.csv`.
    ///
    /// @param rows Every row of the file, in file order: (sex, age, years since intervention, value).
    /// @param path The file, for diagnostics.
    /// @return nullopt if any error was recorded: a duplicate row, a gap, or a value outside [0, 1].
    static std::optional<PifTable> build(std::vector<core::PifDataRow> rows,
                                         const std::filesystem::path &path,
                                         diag::IssueReport &report);

    /// @brief The fraction for a person, or 0 outside the tabulated range.
    ///
    /// Outside the range is 0 rather than an error: a person older than the table's last age, or a
    /// year beyond its horizon, is a real thing a run will meet, and a PIF of zero means "this policy
    /// does nothing here", which is the right answer for an untabulated cell. A *gap inside* the
    /// range is the error case, and it was caught at load.
    double at(unsigned int age, core::Gender gender, int years_since_intervention) const noexcept;

    bool empty() const noexcept { return values_.empty(); }
    std::size_t size() const noexcept { return values_.size(); }

    int min_age() const noexcept { return min_age_; }
    int max_age() const noexcept { return max_age_; }
    int min_year() const noexcept { return min_year_; }
    int max_year() const noexcept { return max_year_; }

    /// @brief The largest fraction in the table, for a load-time note: a table of zeros is a policy
    ///        that does nothing, and that is worth saying out loud.
    double largest() const noexcept { return largest_; }

  private:
    std::vector<double> values_;
    int min_age_{0};
    int max_age_{0};
    int min_year_{0};
    int max_year_{0};
    int ages_{0};
    double largest_{0.0};

    std::size_t offset(int age, core::Gender gender, int year) const noexcept;
};

} // namespace hgps::model
