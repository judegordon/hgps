// Reducing a result CSV to something a chart can take: one value per (scenario, year, variable).
//
// This is the one place the server computes rather than reports, and it earns its place: the
// alternative is every client re-implementing the count-weighted reduction that
// docs/equivalence-method.md had to get right, and getting a different answer. A chart that
// disagreed with the harness about what `mean_bmi` means would be worse than no chart
// (docs/decisions/0042-a-local-server-in-the-same-binary.md).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hgps::server {

/// @brief What to include. Empty means everything.
struct SummaryFilter {
    /// @brief "male", "female" or "all". "all" pools the sexes, weighted by head count.
    std::string sex{"all"};

    /// @brief Variables to include, by name. Empty means every variable in the file.
    std::vector<std::string> variables;
};

/// @brief Reduces a result CSV.
///
/// Every variable in the file is a per-(year, sex, age band) figure. `count`, `deaths` and
/// `emigrations` are counts, so the population figure is their sum; everything else is a mean or a
/// proportion over the band's members, so the population figure is the count-weighted mean. That
/// is the same rule the equivalence harness reduces by, deliberately.
///
/// A (scenario, variable, year) with no members anywhere is `null` rather than zero — the first
/// year of a flow variable, for instance. A client plots a gap; it does not plot a false zero.
///
/// @throws std::runtime_error if the file cannot be read or has no header.
nlohmann::json summarise_results(const std::filesystem::path &csv, const SummaryFilter &filter);

/// @brief The columns a result CSV uses to say which cell a row is, rather than what it measured.
bool is_key_column(const std::string &name) noexcept;

/// @brief True for a column whose population figure is a sum rather than a count-weighted mean.
bool is_counted_column(const std::string &name) noexcept;

} // namespace hgps::server
