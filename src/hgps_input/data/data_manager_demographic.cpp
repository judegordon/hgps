#include "data_manager.h"

#include <rapidcsv.h>

#include "io/json_parser.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace hgps::input {

std::vector<Country> DataManager::get_countries() const {
    auto results = std::vector<Country>();
    auto filepath = index_["country"]["path"].get<std::string>();
    auto filename = index_["country"]["file_name"].get<std::string>();
    filename = (root_ / filepath / filename).string();

    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error(fmt::format("countries file: '{}' not found.", filename));
    }

    rapidcsv::Document doc(filename);
    auto mapping =
        create_fields_index_mapping(doc.GetColumnNames(), {"Code", "Name", "Alpha2", "Alpha3"});
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        results.push_back(Country{.code = std::stoi(row[mapping["Code"]]),
                                  .name = row[mapping["Name"]],
                                  .alpha2 = row[mapping["Alpha2"]],
                                  .alpha3 = row[mapping["Alpha3"]]});
    }

    std::sort(results.begin(), results.end());

    return results;
}

Country DataManager::get_country(const std::string &alpha) const {
    auto c = get_countries();
    auto is_target = [&alpha](const hgps::core::Country &c) {
        return core::case_insensitive::equals(c.alpha2, alpha) ||
               core::case_insensitive::equals(c.alpha3, alpha);
    };

    auto country = std::find_if(c.begin(), c.end(), is_target);
    if (country == c.end()) {
        throw std::runtime_error(fmt::format("Target country: '{}' not found.", alpha));
    }

    return *country;
}

std::vector<PopulationItem> DataManager::get_population(const Country &country) const {
    return DataManager::get_population(country, [](unsigned int) { return true; });
}

std::vector<PopulationItem>
DataManager::get_population(const Country &country,
                            std::function<bool(unsigned int)> time_filter) const {
    auto results = std::vector<PopulationItem>();

    auto nodepath = index_["demographic"]["path"].get<std::string>();
    auto filepath = index_["demographic"]["population"]["path"].get<std::string>();
    auto filename = index_["demographic"]["population"]["file_name"].get<std::string>();

    filename = replace_string_tokens(filename, {std::to_string(country.code)});
    filename = (root_ / nodepath / filepath / filename).string();

    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error(
            fmt::format("{} population file: '{}' not found.", country.name, filename));
    }

    rapidcsv::Document doc(filename);
    auto mapping = create_fields_index_mapping(
        doc.GetColumnNames(), {"LocID", "Time", "Age", "PopMale", "PopFemale", "PopTotal"});

    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        auto row_time = std::stoi(row[mapping["Time"]]);
        if (!time_filter(row_time)) {
            continue;
        }

        results.push_back(PopulationItem{.location_id = std::stoi(row[mapping["LocID"]]),
                                         .at_time = row_time,
                                         .with_age = std::stoi(row[mapping["Age"]]),
                                         .males = std::stof(row[mapping["PopMale"]]),
                                         .females = std::stof(row[mapping["PopFemale"]]),
                                         .total = std::stof(row[mapping["PopTotal"]])});
    }

    std::sort(results.begin(), results.end());

    return results;
}

std::vector<MortalityItem> DataManager::get_mortality(const Country &country) const {
    return DataManager::get_mortality(country, [](unsigned int) { return true; });
}

std::vector<MortalityItem>
DataManager::get_mortality(const Country &country,
                           std::function<bool(unsigned int)> time_filter) const {
    auto results = std::vector<MortalityItem>();
    auto nodepath = index_["demographic"]["path"].get<std::string>();
    auto filepath = index_["demographic"]["mortality"]["path"].get<std::string>();
    auto filename = index_["demographic"]["mortality"]["file_name"].get<std::string>();

    filename = replace_string_tokens(filename, {std::to_string(country.code)});
    filename = (root_ / nodepath / filepath / filename).string();

    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error(
            fmt::format("{} mortality file: '{}' not found.", country.name, filename));
    }

    rapidcsv::Document doc(filename);
    auto mapping = create_fields_index_mapping(
        doc.GetColumnNames(), {"LocID", "Time", "Age", "DeathMale", "DeathFemale", "DeathTotal"});
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        auto row_time = std::stoi(row[mapping["Time"]]);
        if (!time_filter(row_time)) {
            continue;
        }

        results.push_back(MortalityItem{.location_id = std::stoi(row[mapping["LocID"]]),
                                        .at_time = row_time,
                                        .with_age = std::stoi(row[mapping["Age"]]),
                                        .males = std::stof(row[mapping["DeathMale"]]),
                                        .females = std::stof(row[mapping["DeathFemale"]]),
                                        .total = std::stof(row[mapping["DeathTotal"]])});
    }

    std::sort(results.begin(), results.end());

    return results;
}

std::vector<BirthItem> DataManager::get_birth_indicators(const Country &country) const {
    return DataManager::get_birth_indicators(country, [](unsigned int) { return true; });
}

std::vector<BirthItem>
DataManager::get_birth_indicators(const Country &country,
                                  std::function<bool(unsigned int)> time_filter) const {
    auto nodepath = index_["demographic"]["path"].get<std::string>();
    auto filefolder = index_["demographic"]["indicators"]["path"].get<std::string>();
    auto filename = index_["demographic"]["indicators"]["file_name"].get<std::string>();

    auto tokens = std::vector<std::string>{std::to_string(country.code)};
    filename = replace_string_tokens(filename, tokens);
    filename = (root_ / nodepath / filefolder / filename).string();
    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error(fmt::format("{}, demographic indicators file: '{}' not found.",
                                             country.name, filename));
    }

    rapidcsv::Document doc(filename);
    auto mapping = create_fields_index_mapping(doc.GetColumnNames(), {"Time", "Births", "SRB"});
    std::vector<BirthItem> result;
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        auto row_time = std::stoi(row[mapping["Time"]]);
        if (!time_filter(row_time)) {
            continue;
        }

        result.push_back(BirthItem{.at_time = row_time,
                                   .number = std::stof(row[mapping["Births"]]),
                                   .sex_ratio = std::stof(row[mapping["SRB"]])});
    }

    return result;
}

std::vector<LifeExpectancyItem> DataManager::load_life_expectancy(const Country &country) const {
    auto nodepath = index_["demographic"]["path"].get<std::string>();
    auto filefolder = index_["demographic"]["indicators"]["path"].get<std::string>();
    auto filename = index_["demographic"]["indicators"]["file_name"].get<std::string>();

    auto tokens = std::vector<std::string>{std::to_string(country.code)};
    filename = replace_string_tokens(filename, tokens);
    filename = (root_ / nodepath / filefolder / filename).string();
    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error(fmt::format("{}, demographic indicators file: '{}' not found.",
                                             country.name, filename));
    }

    rapidcsv::Document doc(filename);
    auto mapping =
        create_fields_index_mapping(doc.GetColumnNames(), {"Time", "LEx", "LExMale", "LExFemale"});
    std::vector<LifeExpectancyItem> result;
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        if (row.size() < 4) {
            continue;
        }

        result.emplace_back(LifeExpectancyItem{.at_time = std::stoi(row[mapping["Time"]]),
                                               .both = std::stof(row[mapping["LEx"]]),
                                               .male = std::stof(row[mapping["LExMale"]]),
                                               .female = std::stof(row[mapping["LExFemale"]])});
    }

    return result;
}

} // namespace hgps::input