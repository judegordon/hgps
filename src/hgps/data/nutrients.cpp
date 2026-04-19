#include "nutrients.h"

#include "hgps_core/diagnostics/internal_error.h"

#include "fmt/format.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace hgps {
namespace detail {

std::vector<size_t> get_nutrient_indexes(const std::vector<std::string> &column_names,
                                         const std::vector<core::Identifier> &nutrients) {
    std::vector<size_t> nutrient_idx;
    nutrient_idx.reserve(nutrients.size());

    std::vector<std::string> missing;
    missing.reserve(nutrients.size());

    for (const auto &nutrient : nutrients) {
        const auto it = std::find(column_names.begin(), column_names.end(), nutrient);
        if (it == column_names.end()) {
            missing.push_back(nutrient.to_string());
        } else {
            nutrient_idx.push_back(static_cast<size_t>(std::distance(column_names.begin(), it)));
        }
    }

    if (!missing.empty()) {
        throw core::InternalError(
            fmt::format("One or more nutrients were missing from CSV file: {}",
                        fmt::join(missing, ", ")));
    }

    return nutrient_idx;
}

std::unordered_map<core::Identifier, std::vector<double>>
get_nutrient_table(rapidcsv::Document &doc, const std::vector<core::Identifier> &nutrients,
                   const std::vector<size_t> &nutrient_idx) {
    if (nutrients.size() != nutrient_idx.size()) {
        throw core::InternalError("Nutrient column count mismatch while loading nutrient table");
    }

    std::unordered_map<core::Identifier, std::vector<double>> out;
    out.reserve(doc.GetRowCount());

    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        std::vector<double> row;
        row.reserve(nutrients.size());

        for (size_t j = 0; j < nutrients.size(); j++) {
            row.push_back(doc.GetCell<double>(nutrient_idx[j], i));
        }

        const auto row_name = doc.GetRowName(i);
        const auto [it, inserted] = out.emplace(row_name, std::move(row));
        if (!inserted) {
            throw core::InternalError(
                fmt::format("Duplicate nutrient table row name: {}", row_name.to_string()));
        }
    }

    return out;
}

} // namespace detail

std::unordered_map<core::Identifier, std::vector<double>>
load_nutrient_table(const std::string &csv_path, const std::vector<core::Identifier> &nutrients) {
    rapidcsv::Document doc{csv_path, rapidcsv::LabelParams{0, 0}};
    const auto column_names = doc.GetColumnNames();

    const auto nutrient_idx = detail::get_nutrient_indexes(column_names, nutrients);
    return detail::get_nutrient_table(doc, nutrients, nutrient_idx);
}

} // namespace hgps