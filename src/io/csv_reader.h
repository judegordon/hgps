#pragma once

#include "core/datatable.h"
#include "core/identifier.h"
#include "diagnostics/issue_report.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hgps::io {

/// @brief A parsed delimited file: a header row and rows of string fields.
///
/// Every accessor takes an IssueReport and records a located issue rather than throwing, so a
/// data file with twenty bad cells produces twenty diagnostics with line and column numbers. This
/// is what replacing rapidcsv bought (docs/decisions/0014-minimal-dependency-set.md); its errors
/// were exceptions from a third-party header with no location and no issue code.
class CsvDocument {
  public:
    CsvDocument(std::filesystem::path path, std::vector<std::string> headers,
                std::vector<std::vector<std::string>> rows);

    const std::filesystem::path &path() const noexcept { return path_; }
    const std::vector<std::string> &headers() const noexcept { return headers_; }
    std::size_t num_rows() const noexcept { return rows_.size(); }
    std::size_t num_columns() const noexcept { return headers_.size(); }

    /// @brief The index of a column, matched case-insensitively, or nullopt.
    std::optional<std::size_t> column_index(std::string_view name) const;

    /// @brief The raw field. Out-of-range indices are a programmer error and throw.
    const std::string &field(std::size_t row, std::size_t column) const;

    /// @brief The field parsed as a double, or nullopt with a located issue recorded.
    std::optional<double> field_as_double(std::size_t row, std::size_t column,
                                          diag::IssueReport &report) const;
    std::optional<int> field_as_int(std::size_t row, std::size_t column,
                                    diag::IssueReport &report) const;

    /// @brief The 1-based line number in the file of a data row (the header is line 1).
    std::size_t line_of(std::size_t row) const noexcept { return row + 2; }

  private:
    std::filesystem::path path_;
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
};

struct CsvOptions {
    char delimiter{','};

    /// @brief Rows with fewer or more fields than the header are reported as
    ///        IssueCode::csv_ragged_row. Short rows are padded with empty fields so that the rest
    ///        of the file can still be reported on; long rows keep their extra fields.
    bool allow_ragged_rows{false};
};

/// @brief Reads a delimited file.
///
/// Quoted fields are supported: a field may be wrapped in double quotes, and a doubled quote
/// inside one is a literal quote. Leading and trailing whitespace is trimmed from unquoted
/// fields, as the baseline does. A byte-order mark on the first line is skipped.
///
/// @return The document, or nullopt if it could not be read at all — in which case the report
///         says why.
std::optional<CsvDocument> read_csv(const std::filesystem::path &path, const CsvOptions &options,
                                    diag::IssueReport &report);

/// @brief One entry of the config's `inputs.dataset.columns`: a column name and its declared type.
struct CsvColumnSpec {
    std::string name;
    std::string type; // "integer" | "double" | "float" | "string"
};

/// @brief Loads a CSV into a DataTable, taking the columns and their types from the config.
///
/// Columns are matched case-insensitively and appear in the table in the order they are declared
/// in the config, not in the file's order and not alphabetically — so anything that iterates the
/// table sees the order the user wrote. A missing column, an unknown declared type or an
/// unparseable cell is a located issue; every problem in the file is reported, not just the first.
///
/// @return The table, or nullopt if any error was recorded.
std::optional<core::DataTable> load_datatable_from_csv(const std::filesystem::path &path,
                                                       const std::vector<CsvColumnSpec> &columns,
                                                       const CsvOptions &options,
                                                       diag::IssueReport &report);

/// @brief Loads a FactorsMean-style adjustment table: an "age" column then one column per factor.
///
/// Keyed by lower-cased factor name, values in row order. The first column must be `age`.
std::optional<std::map<core::Identifier, std::vector<double>>>
load_baseline_adjustments_from_csv(const std::filesystem::path &path, const CsvOptions &options,
                                  diag::IssueReport &report);

} // namespace hgps::io
