#include "kevinhall_model_parser.h"

#include "config_section_parsing.h"
#include "csv_parser.h"
#include "json_access.h"
#include "json_parser.h"
#include "risk_factor_expected_loader.h"

#include <fmt/core.h>

#include <algorithm>
#include <any>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hgps::input {

std::unique_ptr<hgps::KevinHallModelDefinition>
load_kevinhall_risk_model_definition(const nlohmann::json &opt,
                                     const Configuration &config,
                                     hgps::core::Diagnostics &diagnostics,
                                     std::string_view source_path) {
    auto expected = load_risk_factor_expected(config, diagnostics);
    if (!expected) {
        return nullptr;
    }

    auto expected_trend =
        std::make_unique<std::unordered_map<hgps::core::Identifier, double>>();
    auto trend_steps =
        std::make_unique<std::unordered_map<hgps::core::Identifier, int>>();

    if (!opt.contains("Nutrients") || !opt["Nutrients"].is_array()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path}, .field_path = "Nutrients"},
                          "Missing required key");
        return nullptr;
    }

    if (!opt.contains("Foods") || !opt["Foods"].is_array()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path}, .field_path = "Foods"},
                          "Missing required key");
        return nullptr;
    }

    std::unordered_map<hgps::core::Identifier, double> energy_equation;
    std::unordered_map<hgps::core::Identifier, hgps::core::DoubleInterval> nutrient_ranges;

    for (std::size_t i = 0; i < opt["Nutrients"].size(); ++i) {
        const auto &nutrient = opt["Nutrients"][i];
        const auto nutrient_path = fmt::format("Nutrients[{}]", i);

        bool ok = true;
        hgps::core::Identifier nutrient_key;
        hgps::core::DoubleInterval range;
        double energy = 0.0;

        get_to(nutrient, "Name", nutrient_key, diagnostics, ok, source_path, nutrient_path);
        get_to(nutrient, "Range", range, diagnostics, ok, source_path, nutrient_path);
        get_to(nutrient, "Energy", energy, diagnostics, ok, source_path, nutrient_path);

        if (!ok) {
            return nullptr;
        }

        nutrient_ranges[nutrient_key] = range;
        energy_equation[nutrient_key] = energy;
    }

    std::unordered_map<hgps::core::Identifier, std::map<hgps::core::Identifier, double>>
        nutrient_equations;
    std::unordered_map<hgps::core::Identifier, std::optional<double>> food_prices;

    for (std::size_t i = 0; i < opt["Foods"].size(); ++i) {
        const auto &food = opt["Foods"][i];
        const auto food_path = fmt::format("Foods[{}]", i);

        bool ok = true;
        hgps::core::Identifier food_key;
        std::optional<double> price;
        std::map<hgps::core::Identifier, double> food_nutrients;

        get_to(food, "Name", food_key, diagnostics, ok, source_path, food_path);
        get_to(food, "Price", price, diagnostics, ok, source_path, food_path);
        get_to(food, "Nutrients", food_nutrients, diagnostics, ok, source_path, food_path);

        if (!ok) {
            return nullptr;
        }

        double expected_trend_value = 1.0;
        int trend_steps_value = 0;

        if (food.contains("ExpectedTrend")) {
            try {
                expected_trend_value = food["ExpectedTrend"].get<double>();
            } catch (const nlohmann::json::exception &e) {
                diagnostics.error(
                    hgps::core::DiagnosticCode::wrong_type,
                    {.source_path = std::string{source_path},
                     .field_path = join_field_path(food_path, "ExpectedTrend")},
                    e.what());
                return nullptr;
            }
        }

        if (food.contains("TrendSteps")) {
            try {
                trend_steps_value = food["TrendSteps"].get<int>();
            } catch (const nlohmann::json::exception &e) {
                diagnostics.error(
                    hgps::core::DiagnosticCode::wrong_type,
                    {.source_path = std::string{source_path},
                     .field_path = join_field_path(food_path, "TrendSteps")},
                    e.what());
                return nullptr;
            }
        }

        (*expected_trend)[food_key] = expected_trend_value;
        (*trend_steps)[food_key] = trend_steps_value;
        food_prices[food_key] = price;

        for (const auto &[nutrient_key, unused_range] : nutrient_ranges) {
            const auto nutrient_it = food_nutrients.find(nutrient_key);
            if (nutrient_it != food_nutrients.end()) {
                nutrient_equations[food_key][nutrient_key] = nutrient_it->second;
            }
        }
    }

    if (!opt.contains("WeightQuantiles") || !opt["WeightQuantiles"].is_object()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "WeightQuantiles"},
                          "Missing required key");
        return nullptr;
    }

    if (!opt["WeightQuantiles"].contains("Female")) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "WeightQuantiles.Female"},
                          "Missing required key");
        return nullptr;
    }

    if (!opt["WeightQuantiles"].contains("Male")) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "WeightQuantiles.Male"},
                          "Missing required key");
        return nullptr;
    }

    const auto weight_quantiles_female_file =
        get_file_info(opt["WeightQuantiles"]["Female"], config.root_path, diagnostics, source_path,
                      "WeightQuantiles.Female");
    if (!weight_quantiles_female_file.has_value()) {
        return nullptr;
    }

    const auto weight_quantiles_male_file =
        get_file_info(opt["WeightQuantiles"]["Male"], config.root_path, diagnostics, source_path,
                      "WeightQuantiles.Male");
    if (!weight_quantiles_male_file.has_value()) {
        return nullptr;
    }

    hgps::core::DataTable weight_quantiles_table_female;
    try {
        weight_quantiles_table_female = load_datatable_from_csv(*weight_quantiles_female_file);
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = weight_quantiles_female_file->name.string(),
                           .field_path = "WeightQuantiles.Female"},
                          e.what());
        return nullptr;
    }

    hgps::core::DataTable weight_quantiles_table_male;
    try {
        weight_quantiles_table_male = load_datatable_from_csv(*weight_quantiles_male_file);
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = weight_quantiles_male_file->name.string(),
                           .field_path = "WeightQuantiles.Male"},
                          e.what());
        return nullptr;
    }

    std::unordered_map<hgps::core::Gender, std::vector<double>> weight_quantiles = {
        {hgps::core::Gender::female, {}},
        {hgps::core::Gender::male, {}},
    };

    weight_quantiles[hgps::core::Gender::female].reserve(weight_quantiles_table_female.num_rows());
    weight_quantiles[hgps::core::Gender::male].reserve(weight_quantiles_table_male.num_rows());

    try {
        for (std::size_t row = 0; row < weight_quantiles_table_female.num_rows(); ++row) {
            weight_quantiles[hgps::core::Gender::female].push_back(
                std::any_cast<double>(weight_quantiles_table_female.column(0).value(row)));
        }

        for (std::size_t row = 0; row < weight_quantiles_table_male.num_rows(); ++row) {
            weight_quantiles[hgps::core::Gender::male].push_back(
                std::any_cast<double>(weight_quantiles_table_male.column(0).value(row)));
        }
    } catch (const std::bad_any_cast &) {
        diagnostics.error(hgps::core::DiagnosticCode::wrong_type,
                          {.source_path = std::string{source_path},
                           .field_path = "WeightQuantiles"},
                          "Weight quantiles must contain numeric values");
        return nullptr;
    }

    for (auto &[unused_gender, quantiles] : weight_quantiles) {
        std::sort(quantiles.begin(), quantiles.end());
    }

    if (!opt.contains("EnergyPhysicalActivityQuantiles")) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "EnergyPhysicalActivityQuantiles"},
                          "Missing required key");
        return nullptr;
    }

    const auto epa_quantiles_file =
        get_file_info(opt["EnergyPhysicalActivityQuantiles"], config.root_path, diagnostics,
                      source_path, "EnergyPhysicalActivityQuantiles");
    if (!epa_quantiles_file.has_value()) {
        return nullptr;
    }

    hgps::core::DataTable epa_quantiles_table;
    try {
        epa_quantiles_table = load_datatable_from_csv(*epa_quantiles_file);
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = epa_quantiles_file->name.string(),
                           .field_path = "EnergyPhysicalActivityQuantiles"},
                          e.what());
        return nullptr;
    }

    std::vector<double> epa_quantiles;
    epa_quantiles.reserve(epa_quantiles_table.num_rows());

    try {
        for (std::size_t row = 0; row < epa_quantiles_table.num_rows(); ++row) {
            epa_quantiles.push_back(std::any_cast<double>(epa_quantiles_table.column(0).value(row)));
        }
    } catch (const std::bad_any_cast &) {
        diagnostics.error(hgps::core::DiagnosticCode::wrong_type,
                          {.source_path = std::string{source_path},
                           .field_path = "EnergyPhysicalActivityQuantiles"},
                          "Energy physical activity quantiles must contain numeric values");
        return nullptr;
    }

    std::sort(epa_quantiles.begin(), epa_quantiles.end());

    if (!opt.contains("HeightStdDev") || !opt["HeightStdDev"].is_object()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path}, .field_path = "HeightStdDev"},
                          "Missing required key");
        return nullptr;
    }

    if (!opt.contains("HeightSlope") || !opt["HeightSlope"].is_object()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path}, .field_path = "HeightSlope"},
                          "Missing required key");
        return nullptr;
    }

    std::unordered_map<hgps::core::Gender, double> height_stddev;
    std::unordered_map<hgps::core::Gender, double> height_slope;

    bool ok = true;
    get_to(opt["HeightStdDev"], "Female", height_stddev[hgps::core::Gender::female], diagnostics,
           ok, source_path, "HeightStdDev");
    get_to(opt["HeightStdDev"], "Male", height_stddev[hgps::core::Gender::male], diagnostics, ok,
           source_path, "HeightStdDev");
    get_to(opt["HeightSlope"], "Female", height_slope[hgps::core::Gender::female], diagnostics,
           ok, source_path, "HeightSlope");
    get_to(opt["HeightSlope"], "Male", height_slope[hgps::core::Gender::male], diagnostics, ok,
           source_path, "HeightSlope");
    if (!ok) {
        return nullptr;
    }

    try {
        return std::make_unique<hgps::KevinHallModelDefinition>(
            std::move(expected), std::move(expected_trend), std::move(trend_steps),
            std::move(energy_equation), std::move(nutrient_ranges), std::move(nutrient_equations),
            std::move(food_prices), std::move(weight_quantiles), std::move(epa_quantiles),
            std::move(height_stddev), std::move(height_slope));
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = std::string{source_path}},
                          e.what());
        return nullptr;
    } catch (...) {
        diagnostics.error(
            hgps::core::DiagnosticCode::parse_failure,
            {.source_path = std::string{source_path}},
            "Unknown error while constructing Kevin Hall model definition");
        return nullptr;
    }
}

} // namespace hgps::input