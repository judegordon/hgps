#include "csv_reader.h"

#include "core/string_util.h"
#include "diagnostics/internal_error.h"

#include <fstream>
#include <utility>

#include <fmt/format.h>

namespace hgps::io {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

/// Splits one line into fields, honouring double-quoted fields.
std::vector<std::string> split_line(std::string_view line, char delimiter) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    bool was_quoted = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                current += c;
            }
            continue;
        }

        if (c == '"' && current.empty()) {
            in_quotes = true;
            was_quoted = true;
            continue;
        }

        if (c == delimiter) {
            fields.push_back(was_quoted ? current : core::trim(current));
            current.clear();
            was_quoted = false;
            continue;
        }

        current += c;
    }

    fields.push_back(was_quoted ? current : core::trim(current));
    return fields;
}

void strip_bom(std::string &line) {
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }
}

void strip_carriage_return(std::string &line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

} // namespace

CsvDocument::CsvDocument(std::filesystem::path path, std::vector<std::string> headers,
                         std::vector<std::vector<std::string>> rows)
    : path_{std::move(path)}, headers_{std::move(headers)}, rows_{std::move(rows)} {}

std::optional<std::size_t> CsvDocument::column_index(std::string_view name) const {
    for (std::size_t i = 0; i < headers_.size(); ++i) {
        if (core::case_insensitive::equals(headers_[i], name)) {
            return i;
        }
    }
    return std::nullopt;
}

const std::string &CsvDocument::field(std::size_t row, std::size_t column) const {
    if (row >= rows_.size() || column >= rows_[row].size()) {
        throw diag::InternalError(fmt::format("CSV field ({}, {}) is outside {} rows x {} columns "
                                              "in {}",
                                              row, column, rows_.size(), headers_.size(),
                                              path_.string()));
    }
    return rows_[row][column];
}

std::optional<double> CsvDocument::field_as_double(std::size_t row, std::size_t column,
                                                   diag::IssueReport &report) const {
    const auto &text = field(row, column);
    if (text.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return value;
    } catch (const std::exception &) {
        // The one permitted shape of catch: convert a third-party or standard-library exception
        // into a located issue at the boundary (docs/decisions/0018).
        report.error(IssueCode::csv_bad_value,
                     IssueLocation{.file = path_.string(),
                                   .field = column < headers_.size() ? headers_[column] : "",
                                   .line = line_of(row),
                                   .column = column + 1},
                     fmt::format("'{}' is not a number", text));
        return std::nullopt;
    }
}

std::optional<int> CsvDocument::field_as_int(std::size_t row, std::size_t column,
                                             diag::IssueReport &report) const {
    const auto &text = field(row, column);
    if (text.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return value;
    } catch (const std::exception &) {
        report.error(IssueCode::csv_bad_value,
                     IssueLocation{.file = path_.string(),
                                   .field = column < headers_.size() ? headers_[column] : "",
                                   .line = line_of(row),
                                   .column = column + 1},
                     fmt::format("'{}' is not an integer", text));
        return std::nullopt;
    }
}

std::optional<CsvDocument> read_csv(const std::filesystem::path &path, const CsvOptions &options,
                                    diag::IssueReport &report) {
    std::ifstream stream{path};
    if (!stream) {
        report.error(IssueCode::file_not_found, IssueLocation{.file = path.string()},
                     "cannot open the file for reading");
        return std::nullopt;
    }

    std::string line;
    if (!std::getline(stream, line)) {
        report.error(IssueCode::csv_empty, IssueLocation{.file = path.string()},
                     "the file is empty: a header row is required");
        return std::nullopt;
    }

    strip_bom(line);
    strip_carriage_return(line);
    auto headers = split_line(line, options.delimiter);

    if (headers.empty() || (headers.size() == 1 && headers.front().empty())) {
        report.error(IssueCode::csv_empty, IssueLocation{.file = path.string(), .line = 1U},
                     "the header row has no columns");
        return std::nullopt;
    }

    for (std::size_t i = 0; i < headers.size(); ++i) {
        for (std::size_t j = i + 1; j < headers.size(); ++j) {
            if (core::case_insensitive::equals(headers[i], headers[j])) {
                report.error(IssueCode::csv_duplicate_column,
                             IssueLocation{.file = path.string(),
                                           .field = headers[i],
                                           .line = 1U,
                                           .column = j + 1},
                             fmt::format("column '{}' appears twice, at positions {} and {}",
                                         headers[i], i + 1, j + 1));
            }
        }
    }

    std::vector<std::vector<std::string>> rows;
    std::size_t line_number = 1;
    while (std::getline(stream, line)) {
        ++line_number;
        strip_carriage_return(line);

        // A wholly empty trailing line is file punctuation, not a row.
        if (line.empty()) {
            continue;
        }

        auto fields = split_line(line, options.delimiter);
        if (fields.size() != headers.size()) {
            if (!options.allow_ragged_rows) {
                report.error(IssueCode::csv_ragged_row,
                             IssueLocation{.file = path.string(), .line = line_number},
                             fmt::format("row has {} field{}, the header has {}", fields.size(),
                                         fields.size() == 1 ? "" : "s", headers.size()));
            }
            fields.resize(std::max(fields.size(), headers.size()));
        }

        rows.push_back(std::move(fields));
    }

    return CsvDocument{path, std::move(headers), std::move(rows)};
}

std::optional<core::DataTable> load_datatable_from_csv(const std::filesystem::path &path,
                                                       const std::vector<CsvColumnSpec> &columns,
                                                       const CsvOptions &options,
                                                       diag::IssueReport &report) {
    const auto before = report.error_count();
    const auto document = read_csv(path, options, report);
    if (!document) {
        return std::nullopt;
    }

    core::DataTable table;

    // Declared order, not file order and not alphabetical. The baseline built this map with
    // std::map keyed by column name, so the table's column order was alphabetical and one of its
    // model loaders had to go behind the loader's back to recover the file's order.
    for (const auto &spec : columns) {
        const auto index = document->column_index(spec.name);
        if (!index) {
            report.error(IssueCode::csv_missing_column,
                         IssueLocation{.file = path.string(), .field = spec.name, .line = 1U},
                         fmt::format("column '{}' is declared in the config but not in the file",
                                     spec.name));
            continue;
        }

        const auto type = core::to_lower(spec.type);
        const auto rows = document->num_rows();

        if (type == "integer") {
            core::IntegerDataTableColumnBuilder builder{spec.name};
            builder.reserve(rows);
            for (std::size_t row = 0; row < rows; ++row) {
                const auto value = document->field_as_int(row, *index, report);
                value ? builder.append(*value) : builder.append_null();
            }
            table.add(builder.build());
        } else if (type == "double") {
            core::DoubleDataTableColumnBuilder builder{spec.name};
            builder.reserve(rows);
            for (std::size_t row = 0; row < rows; ++row) {
                const auto value = document->field_as_double(row, *index, report);
                value ? builder.append(*value) : builder.append_null();
            }
            table.add(builder.build());
        } else if (type == "float") {
            core::FloatDataTableColumnBuilder builder{spec.name};
            builder.reserve(rows);
            for (std::size_t row = 0; row < rows; ++row) {
                const auto value = document->field_as_double(row, *index, report);
                value ? builder.append(static_cast<float>(*value)) : builder.append_null();
            }
            table.add(builder.build());
        } else if (type == "string") {
            core::StringDataTableColumnBuilder builder{spec.name};
            builder.reserve(rows);
            for (std::size_t row = 0; row < rows; ++row) {
                const auto &text = document->field(row, *index);
                text.empty() ? builder.append_null() : builder.append(text);
            }
            table.add(builder.build());
        } else {
            report.error(IssueCode::config_bad_value,
                         IssueLocation{.file = path.string(), .field = spec.name},
                         fmt::format("unknown column type '{}'; expected integer, double, float "
                                     "or string",
                                     spec.type));
        }
    }

    if (report.error_count() != before) {
        return std::nullopt;
    }

    return table;
}

std::optional<std::map<core::Identifier, std::vector<double>>>
load_baseline_adjustments_from_csv(const std::filesystem::path &path, const CsvOptions &options,
                                  diag::IssueReport &report) {
    const auto before = report.error_count();
    const auto document = read_csv(path, options, report);
    if (!document) {
        return std::nullopt;
    }

    if (document->num_columns() < 2) {
        report.error(IssueCode::csv_missing_column,
                     IssueLocation{.file = path.string(), .line = 1U},
                     fmt::format("an adjustment file needs an 'age' column and at least one "
                                 "factor column; this one has {}",
                                 document->num_columns()));
        return std::nullopt;
    }

    if (!core::case_insensitive::equals(document->headers().front(), "age")) {
        report.error(IssueCode::csv_missing_column,
                     IssueLocation{.file = path.string(),
                                   .field = document->headers().front(),
                                   .line = 1U,
                                   .column = 1U},
                     fmt::format("the first column of an adjustment file must be 'age', not '{}'",
                                 document->headers().front()));
        return std::nullopt;
    }

    std::map<core::Identifier, std::vector<double>> result;
    for (std::size_t column = 1; column < document->num_columns(); ++column) {
        std::vector<double> values;
        values.reserve(document->num_rows());
        for (std::size_t row = 0; row < document->num_rows(); ++row) {
            const auto value = document->field_as_double(row, column, report);
            values.push_back(value.value_or(0.0));
        }
        result.emplace(core::Identifier{document->headers()[column]}, std::move(values));
    }

    if (report.error_count() != before) {
        return std::nullopt;
    }

    return result;
}

} // namespace hgps::io
