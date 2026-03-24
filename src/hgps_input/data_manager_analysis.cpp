#include "data_manager.h"

#include <rapidcsv.h>

#include <filesystem>
#include "json_parser.h"
#include <stdexcept>

namespace hgps::input {

std::vector<LmsDataRow> DataManager::get_lms_parameters() const {
    auto analysis_folder = index_["analysis"]["path"].get<std::string>();
    auto lms_filename = index_["analysis"]["lms_file_name"].get<std::string>();
    auto full_filename = (root_ / analysis_folder / lms_filename);
    if (!std::filesystem::exists(full_filename)) {
        throw std::runtime_error(
            fmt::format("LMS parameters file: '{}' not found.", full_filename.string()));
    }

    rapidcsv::Document doc(full_filename.string());
    auto mapping = create_fields_index_mapping(doc.GetColumnNames(),
                                               {"age", "gender_id", "lambda", "mu", "sigma"});
    auto parameters = std::vector<LmsDataRow>{};
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        if (row.size() < 6) {
            continue;
        }

        parameters.emplace_back(
            LmsDataRow{.age = std::stoi(row[mapping["age"]]),
                       .gender = static_cast<core::Gender>(std::stoi(row[mapping["gender_id"]])),
                       .lambda = std::stod(row[mapping["lambda"]]),
                       .mu = std::stod(row[mapping["mu"]]),
                       .sigma = std::stod(row[mapping["sigma"]])});
    }

    return parameters;
}

DiseaseAnalysisEntity DataManager::get_disease_analysis(const Country &country) const {
    auto analysis_folder = index_["analysis"]["path"].get<std::string>();
    auto disability_filename = index_["analysis"]["disability_file_name"].get<std::string>();
    const auto &cost_node = index_["analysis"]["cost_of_disease"];

    auto local_root_path = (root_ / analysis_folder);
    disability_filename = (local_root_path / disability_filename).string();
    if (!std::filesystem::exists(disability_filename)) {
        throw std::runtime_error(
            fmt::format("Disease disability weights file: '{}' not found.", disability_filename));
    }

    rapidcsv::Document doc(disability_filename);
    DiseaseAnalysisEntity entity;
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        if (row.size() < 2) {
            continue;
        }

        entity.disability_weights.emplace(row[0], std::stof(row[1]));
    }

    entity.cost_of_diseases = load_cost_of_diseases(country, cost_node, local_root_path);
    entity.life_expectancy = load_life_expectancy(country);
    return entity;
}

std::map<int, std::map<Gender, double>>
DataManager::load_cost_of_diseases(const Country &country, const nlohmann::json &node,
                                   const std::filesystem::path &parent_path) const {
    auto cost_path = node["path"].get<std::string>();
    auto filename = node["file_name"].get<std::string>();

    auto tokens = std::vector<std::string>{std::to_string(country.code)};
    filename = replace_string_tokens(filename, tokens);
    filename = (parent_path / cost_path / filename).string();
    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error(
            fmt::format("{} cost of disease file: '{}' not found.", country.name, filename));
    }

    rapidcsv::Document doc(filename);
    std::map<int, std::map<Gender, double>> result;
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        if (row.size() < 13) {
            continue;
        }

        auto age = std::stoi(row[6]);
        auto gender = static_cast<core::Gender>(std::stoi(row[8]));
        result[age][gender] = std::stod(row[12]);
    }

    return result;
}

} // namespace hgps::input