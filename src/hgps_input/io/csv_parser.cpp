#include "csv_parser.h"

#include <rapidcsv.h>

#include "hgps/data/gender_value.h"
#include "hgps_core/utils/scoped_timer.h"
#include "hgps_core/utils/string_util.h"


#include <fmt/color.h>
#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if USE_TIMER
#define MEASURE_FUNCTION() \
    hgps::core::ScopedTimer timer{__func__}
#else
#define MEASURE_FUNCTION()
#endif

namespace {

namespace hc = hgps::core;

char get_delimiter(const std::string& delimiter) {
    if (delimiter.empty()) {
        throw std::invalid_argument("CSV delimiter must not be empty.");
    }

    return delimiter.front();
}

hc::DataTableColumn parse_string_column(const std::string& name,
                                        const std::vector<std::string>& data) {
    hc::DataTableColumnBuilder<std::string> builder{name};

    for (const auto& value : data) {
        auto str = hc::trim(value);
        if (!str.empty()) {
            builder.append(std::move(str));
        } else {
            builder.append_null();
        }
    }

    return builder.build();
}

template <typename T, typename ParseFn>
hc::DataTableColumn parse_numeric_column(const std::string& name,
                                         const std::vector<std::string>& data,
                                         ParseFn parse_value) {
    hc::DataTableColumnBuilder<T> builder{name};

    for (const auto& value : data) {
        auto str = hc::trim(value);
        if (!str.empty()) {
            builder.append(parse_value(str));
        } else {
            builder.append_null();
        }
    }

    return builder.build();
}

} // namespace

namespace hgps::input {

hgps::core::DataTable load_datatable_from_csv(const FileInfo& file_info) {
    MEASURE_FUNCTION();
    using namespace rapidcsv;

    bool success = true;
    Document doc{
        file_info.name.string(),
        LabelParams{},
        SeparatorParams{get_delimiter(file_info.delimiter)},
    };

    auto headers = doc.GetColumnNames();
    std::map<std::string, std::string, hc::case_insensitive::comparator> csv_column_map;

    for (const auto& pair : file_info.columns) {
        const std::string& col_name = pair.first;

        auto is_match = [&col_name](const auto& csv_col_name) {
            return hc::case_insensitive::equals(col_name, csv_col_name);
        };

        auto it = std::find_if(headers.begin(), headers.end(), is_match);
        if (it != headers.end()) {
            csv_column_map[col_name] = *it;
        } else {
            success = false;
            fmt::print(fg(fmt::color::dark_salmon), "Column: {} not found in dataset.\n", col_name);
        }
    }

    if (!success) {
        throw std::runtime_error("Required columns not found in dataset.");
    }

    hc::DataTable out_table;
    for (const auto& [col_name, csv_col_name] : csv_column_map) {
        const std::string col_type = hc::to_lower(file_info.columns.at(col_name));
        const auto data = doc.GetColumn<std::string>(csv_col_name);

        try {
            if (col_type == "integer") {
                out_table.add(parse_numeric_column<int>(col_name, data, [](const std::string& s) {
                    return std::stoi(s);
                }));
            } else if (col_type == "double") {
                out_table.add(
                    parse_numeric_column<double>(col_name, data, [](const std::string& s) {
                        return std::stod(s);
                    }));
            } else if (col_type == "float") {
                out_table.add(parse_numeric_column<float>(col_name, data, [](const std::string& s) {
                    return std::stof(s);
                }));
            } else if (col_type == "string") {
                out_table.add(parse_string_column(col_name, data));
            } else {
                fmt::print(fg(fmt::color::dark_salmon), "Unknown data type: {} in column: {}\n",
                           col_type, col_name);
                success = false;
            }
        } catch (const std::exception& ex) {
            fmt::print(fg(fmt::color::red), "Error parsing column: {}, cause: {}\n", col_name,
                       ex.what());
            success = false;
        }
    }

    if (!success) {
        throw std::runtime_error("Error parsing dataset.");
    }

    return out_table;
}

std::unordered_map<hgps::core::Identifier, std::vector<double>>
load_baseline_from_csv(const std::string& filename, const std::string& delimiter) {
    using namespace rapidcsv;

    auto data = std::unordered_map<std::string, std::vector<double>>{};
    auto doc = Document{filename, LabelParams{}, SeparatorParams(get_delimiter(delimiter))};
    auto column_names = doc.GetColumnNames();
    auto column_count = column_names.size();

    if (column_count < 2) {
        throw std::invalid_argument(fmt::format(
            "Invalid number of columns: {} in adjustment file: {}", column_names.size(), filename));
    }

    if (!hc::case_insensitive::equals("age", column_names.at(0))) {
        throw std::invalid_argument(
            fmt::format("Invalid adjustment file format, first column must be age: {}", filename));
    }

    std::transform(column_names.begin(), column_names.end(), column_names.begin(), hc::to_lower);

    for (std::size_t col = 1; col < column_count; ++col) {
        data.emplace(column_names.at(col), std::vector<double>{});
    }

    for (std::size_t row_index = 0; row_index < doc.GetRowCount(); ++row_index) {
        const auto row = doc.GetRow<std::string>(row_index);

        for (std::size_t col = 1; col < row.size(); ++col) {
            const auto value = hc::trim(row[col]);
            data.at(column_names[col]).emplace_back(std::stod(value));
        }
    }

    for (auto& col : data) {
        col.second.shrink_to_fit();
    }

    auto result = std::unordered_map<hc::Identifier, std::vector<double>>{};
    for (const auto& col : data) {
        result.emplace(hc::Identifier{col.first}, col.second);
    }

    return result;
}

} // namespace hgps::input