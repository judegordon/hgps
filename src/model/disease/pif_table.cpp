#include "pif_table.h"

#include "diagnostics/internal_error.h"

#include <algorithm>
#include <map>

#include <fmt/format.h>

namespace hgps::model {
namespace {

constexpr int kSexes = 2;

int sex_index(core::Gender gender) noexcept { return gender == core::Gender::male ? 0 : 1; }

} // namespace

std::optional<PifTable> PifTable::build(std::vector<core::PifDataRow> rows,
                                        const std::filesystem::path &path,
                                        diag::IssueReport &report) {
    if (rows.empty()) {
        report.error(diag::IssueCode::csv_empty, diag::IssueLocation{.file = path.string()},
                     "a population impact fraction table with no rows");
        return std::nullopt;
    }

    PifTable table;
    table.min_age_ = rows.front().age;
    table.max_age_ = rows.front().age;
    table.min_year_ = rows.front().years_since_intervention;
    table.max_year_ = rows.front().years_since_intervention;

    bool bad_value = false;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        table.min_age_ = std::min(table.min_age_, row.age);
        table.max_age_ = std::max(table.max_age_, row.age);
        table.min_year_ = std::min(table.min_year_, row.years_since_intervention);
        table.max_year_ = std::max(table.max_year_, row.years_since_intervention);

        // Upstream clamps a value outside [0, 1] into range and warns. A fraction above one makes an
        // incidence probability negative and a fraction below zero makes a policy cause disease;
        // neither is a number this program can act on, so both are errors naming the row.
        if (!(row.value >= 0.0 && row.value <= 1.0)) {
            report.error(diag::IssueCode::csv_bad_value,
                         diag::IssueLocation{.file = path.string(),
                                             .field = "IF_Mean",
                                             .line = i + 2},
                         fmt::format("a population impact fraction of {} is outside [0, 1]; it would "
                                     "make an incidence probability negative or a policy cause "
                                     "disease. Upstream clamps this value and warns",
                                     row.value));
            bad_value = true;
        }
    }

    if (table.min_age_ < 0) {
        report.error(diag::IssueCode::csv_bad_value,
                     diag::IssueLocation{.file = path.string(), .field = "Age"},
                     fmt::format("a negative age, {}", table.min_age_));
        return std::nullopt;
    }
    if (table.min_year_ < 0) {
        report.error(diag::IssueCode::csv_bad_value,
                     diag::IssueLocation{.file = path.string(), .field = "YearPostInt"},
                     fmt::format("a negative number of years since the intervention, {}",
                                 table.min_year_));
        return std::nullopt;
    }
    if (bad_value) {
        return std::nullopt;
    }

    table.ages_ = table.max_age_ - table.min_age_ + 1;
    const auto years = table.max_year_ - table.min_year_ + 1;
    const auto cells = static_cast<std::size_t>(years) * static_cast<std::size_t>(kSexes) *
                       static_cast<std::size_t>(table.ages_);

    // Filled with a sentinel rather than zero, so a gap is distinguishable from a tabulated zero —
    // which the file is full of, and which is a meaningful value.
    constexpr double kMissing = -1.0;
    table.values_.assign(cells, kMissing);

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        const auto at = table.offset(row.age, row.gender, row.years_since_intervention);
        if (table.values_[at] != kMissing) {
            report.error(diag::IssueCode::csv_bad_value,
                         diag::IssueLocation{.file = path.string(), .line = i + 2},
                         fmt::format("a second population impact fraction for sex {}, age {}, year "
                                     "{} after the intervention; the first was {}",
                                     row.gender == core::Gender::male ? "male" : "female", row.age,
                                     row.years_since_intervention, table.values_[at]));
            return std::nullopt;
        }
        table.values_[at] = row.value;
        table.largest_ = std::max(table.largest_, row.value);
    }

    // Completeness. A gap reads as a PIF of zero in upstream's dense array, so half a file produces a
    // policy that works for half the population with nothing said.
    for (int year = table.min_year_; year <= table.max_year_; ++year) {
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            for (int age = table.min_age_; age <= table.max_age_; ++age) {
                if (table.values_[table.offset(age, gender, year)] == kMissing) {
                    report.error(
                        diag::IssueCode::csv_bad_value,
                        diag::IssueLocation{.file = path.string()},
                        fmt::format(
                            "no population impact fraction for sex {}, age {}, year {} after the "
                            "intervention, while the file covers ages {}–{} and years {}–{}. "
                            "Upstream would read that cell as exactly zero",
                            gender == core::Gender::male ? "male" : "female", age, year,
                            table.min_age_, table.max_age_, table.min_year_, table.max_year_));
                    return std::nullopt;
                }
            }
        }
    }

    return table;
}

std::size_t PifTable::offset(int age, core::Gender gender, int year) const noexcept {
    const auto y = static_cast<std::size_t>(year - min_year_);
    const auto s = static_cast<std::size_t>(sex_index(gender));
    const auto a = static_cast<std::size_t>(age - min_age_);
    return ((y * static_cast<std::size_t>(kSexes)) + s) * static_cast<std::size_t>(ages_) + a;
}

double PifTable::at(unsigned int age, core::Gender gender,
                    int years_since_intervention) const noexcept {
    if (values_.empty()) {
        return 0.0;
    }
    const auto age_value = static_cast<int>(age);
    if (age_value < min_age_ || age_value > max_age_ || years_since_intervention < min_year_ ||
        years_since_intervention > max_year_) {
        return 0.0;
    }
    // An unknown sex has no row in any of these tables, and a policy cannot be applied to somebody
    // the table does not describe.
    if (gender != core::Gender::male && gender != core::Gender::female) {
        return 0.0;
    }
    return values_[offset(age_value, gender, years_since_intervention)];
}

} // namespace hgps::model
