#include "data_manager.h"

#include <fmt/color.h>
#include <rapidcsv.h>
#include "json_parser.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <stdexcept>

namespace hgps::input {

std::optional<PIFData> DataManager::get_pif_data(const DiseaseInfo &disease_info,
                                                 const Country &country,
                                                 const nlohmann::json &pif_config) const {
    if (!pif_config.contains("enabled") || !pif_config["enabled"].get<bool>()) {
        return std::nullopt;
    }

    auto data_root_path = pif_config["data_root_path"].get<std::string>();
    auto risk_factor = pif_config["risk_factor"].get<std::string>();
    auto scenario = pif_config["scenario"].get<std::string>();

    auto country_code = std::to_string(country.code);
    auto csv_filename = "IF" + country_code + ".csv";

    // FIX: define pif_path before using it
    auto pif_path = construct_pif_path(disease_info.code.to_string(), pif_config);
    auto full_path = pif_path / csv_filename;

    if (std::filesystem::exists(full_path)) {
        try {
            auto pif_table = load_pif_from_csv(full_path);
            if (pif_table.has_data()) {
                PIFData pif_data;
                pif_data.add_scenario_data(scenario, std::move(pif_table));

                const auto *loaded_pif_table = pif_data.get_scenario_data(scenario);
                auto file_size = std::filesystem::file_size(full_path);

                fmt::print(fg(fmt::color::green), "PIF Data Loaded Successfully:\n");
                fmt::print("  - Disease: {}\n", disease_info.code.to_string());
                fmt::print("  - Country: {} (Code: {})\n", country.name, country_code);
                fmt::print("  - Risk Factor: {}\n", risk_factor);
                fmt::print("  - Scenario: {}\n", scenario);
                fmt::print("  - Total Data Rows: {}\n", loaded_pif_table->size());
                fmt::print("  - File Size: {} bytes ({:.2f} KB)\n", file_size, file_size / 1024.0);
                fmt::print("  - Memory Usage: {} bytes ({:.2f} KB)\n",
                           loaded_pif_table->size() * sizeof(PIFDataItem),
                           (loaded_pif_table->size() * sizeof(PIFDataItem)) / 1024.0);
                fmt::print("  - File: {}\n", csv_filename);
                fmt::print("  - Path: {}\n", full_path.string());
                fmt::print("  - PIF Analysis: ENABLED and READY\n\n");

                return pif_data;
            }
        } catch (const std::exception &e) {
            notify_warning(
                fmt::format("Failed to load PIF data from {}: {}", full_path.string(), e.what()));
        }
    }

    notify_warning(fmt::format(
        "PIF data not found for disease {} in scenario {} at path {} (expected file: {})",
        disease_info.code.to_string(), scenario, pif_path.string(), csv_filename));
    return std::nullopt;
}

std::filesystem::path DataManager::construct_pif_path(const std::string &disease_code,
                                                      const nlohmann::json &pif_config) const {
    auto data_root_path = pif_config["data_root_path"].get<std::string>();
    auto risk_factor = pif_config["risk_factor"].get<std::string>();
    auto scenario = pif_config["scenario"].get<std::string>();

    data_root_path = expand_environment_variables(data_root_path);

    return std::filesystem::path(data_root_path) / "diseases" / disease_code / "PIF" / risk_factor /
           scenario;
}

std::string DataManager::expand_environment_variables(const std::string &path) const {
    std::string result = path;

    std::regex env_var_regex(R"(\$\{([^}]+)\})");
    std::smatch match;

    while (std::regex_search(result, match, env_var_regex)) {
        std::string var_name = match[1].str();
        const char *env_value = std::getenv(var_name.c_str());

        if (env_value) {
            result.replace(match.position(), match.length(), env_value);
        } else {
            notify_warning(
                fmt::format("Environment variable {} not found, using placeholder", var_name));
        }
    }

    return result;
}

PIFTable DataManager::load_pif_from_csv(const std::filesystem::path &filepath) const {
    PIFTable table;
    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        rapidcsv::Document doc(filepath.string());
        auto mapping = create_fields_index_mapping(doc.GetColumnNames(),
                                                   {"Gender", "Age", "YearPostInt", "IF_Mean"});

        for (size_t i = 0; i < doc.GetRowCount(); i++) {
            auto row = doc.GetRow<std::string>(i);

            PIFDataItem item{};
            int csv_gender = std::stoi(row[mapping["Gender"]]);
            item.gender = (csv_gender == 0) ? core::Gender::male : core::Gender::female;
            item.age = std::stoi(row[mapping["Age"]]);
            item.year_post_intervention = std::stoi(row[mapping["YearPostInt"]]);
            item.pif_value = std::stod(row[mapping["IF_Mean"]]);

            if (item.pif_value < 0.0 || item.pif_value > 1.0) {
                notify_warning(fmt::format("PIF value {} out of range [0.0, 1.0] at row {}",
                                           item.pif_value, i + 1));
                item.pif_value = std::clamp(item.pif_value, 0.0, 1.0);
            }

            table.add_item(item);
        }

        table.build_hash_table();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        fmt::print(fg(fmt::color::cyan), "PIF CSV File Loaded: {} ({} rows) in {}ms\n",
                   filepath.filename().string(), table.size(), duration.count());
        fmt::print(fg(fmt::color::green),
                   "PIF Hash Table Built: O(1) lookup optimization enabled\n");

    } catch (const std::exception &e) {
        throw std::runtime_error(
            fmt::format("Failed to load PIF CSV file {}: {}", filepath.string(), e.what()));
    }

    return table;
}

} // namespace hgps::input