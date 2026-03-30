#include "data_manager.h"

#include "hgps_core/math_util.h"

#include <rapidcsv.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include "io/json_parser.h"
#include <unordered_map>


namespace hgps::input {

std::vector<DiseaseInfo> DataManager::get_diseases() const {
    auto result = std::vector<DiseaseInfo>();

    const auto &registry = index_["diseases"]["registry"];
    for (const auto &item : registry) {
        auto info = DiseaseInfo{};
        auto group_str = std::string{};
        auto code_srt = std::string{};
        item["group"].get_to(group_str);
        item["id"].get_to(code_srt);
        item["name"].get_to(info.name);
        info.group = DiseaseGroup::other;
        info.code = Identifier{code_srt};
        if (core::case_insensitive::equals(group_str, "cancer")) {
            info.group = DiseaseGroup::cancer;
        }

        result.emplace_back(info);
    }

    std::sort(result.begin(), result.end());

    return result;
}

DiseaseInfo DataManager::get_disease_info(const core::Identifier &code) const {
    const auto &registry = index_["diseases"]["registry"];
    const auto &disease_code_str = code.to_string();
    auto info = DiseaseInfo{};
    for (const auto &item : registry) {
        auto item_code_str = std::string{};
        item["id"].get_to(item_code_str);
        if (item_code_str == disease_code_str) {
            auto group_str = std::string{};
            item["group"].get_to(group_str);
            item["name"].get_to(info.name);
            info.code = code;
            info.group = DiseaseGroup::other;
            if (core::case_insensitive::equals(group_str, "cancer")) {
                info.group = DiseaseGroup::cancer;
            }

            return info;
        }
    }

    throw std::runtime_error(fmt::format("Disease code: '{}' not found.", code.to_string()));
}

DiseaseEntity DataManager::get_disease(const DiseaseInfo &info, const Country &country) const {
    DiseaseEntity result;
    result.info = info;
    result.country = country;

    auto diseases_path = index_["diseases"]["path"].get<std::string>();
    auto disease_folder = index_["diseases"]["disease"]["path"].get<std::string>();
    auto filename = index_["diseases"]["disease"]["file_name"].get<std::string>();

    disease_folder = replace_string_tokens(disease_folder, {info.code.to_string()});
    filename = replace_string_tokens(filename, {std::to_string(country.code)});

    filename = (root_ / diseases_path / disease_folder / filename).string();
    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error(
            fmt::format("{}, {} file: '{}' not found.", country.name, info.name, filename));
    }

    rapidcsv::Document doc(filename);

    auto table = std::unordered_map<int, std::unordered_map<core::Gender, std::map<int, double>>>();

    auto mapping = create_fields_index_mapping(
        doc.GetColumnNames(), {"age", "gender_id", "measure_id", "measure", "mean"});
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);
        auto age = std::stoi(row[mapping["age"]]);
        auto gender = static_cast<core::Gender>(std::stoi(row[mapping["gender_id"]]));
        auto measure_id = std::stoi(row[mapping["measure_id"]]);
        auto measure_name = core::to_lower(row[mapping["measure"]]);
        auto measure_value = std::stod(row[mapping["mean"]]);

        if (!result.measures.contains(measure_name)) {
            result.measures.emplace(measure_name, measure_id);
        }

        if (!table[age].contains(gender)) {
            table[age].emplace(gender, std::map<int, double>{});
        }

        table[age][gender][measure_id] = measure_value;
    }

    for (auto &pair : table) {
        for (auto &child : pair.second) {
            result.items.emplace_back(DiseaseItem{
                .with_age = pair.first, .gender = child.first, .measures = child.second});
        }
    }

    result.items.shrink_to_fit();

    return result;
}

std::optional<RelativeRiskEntity>
DataManager::get_relative_risk_to_disease(const DiseaseInfo &source,
                                          const DiseaseInfo &target) const {
    auto diseases_path = index_["diseases"]["path"].get<std::string>();
    auto disease_folder = index_["diseases"]["disease"]["path"].get<std::string>();
    const auto &risk_node = index_["diseases"]["disease"]["relative_risk"];
    auto default_value = risk_node["to_disease"]["default_value"].get<float>();

    auto risk_folder = risk_node["path"].get<std::string>();
    auto file_folder = risk_node["to_disease"]["path"].get<std::string>();
    auto filename = risk_node["to_disease"]["file_name"].get<std::string>();

    auto source_code_str = source.code.to_string();
    disease_folder = replace_string_tokens(disease_folder, std::vector<std::string>{source_code_str});

    auto target_code_str = target.code.to_string();
    auto tokens = {source_code_str, target_code_str};
    filename = replace_string_tokens(filename, tokens);

    filename =
        (root_ / diseases_path / disease_folder / risk_folder / file_folder / filename).string();
    if (!std::filesystem::exists(filename)) {
        notify_warning(
            fmt::format("{} to {} relative risk file not found", source_code_str, target_code_str));
        return std::nullopt;
    }

    rapidcsv::Document doc(filename);

    auto table = RelativeRiskEntity();
    table.columns = doc.GetColumnNames();

    auto row_size = table.columns.size();
    auto non_default_count = std::size_t{0};
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);

        auto row_data = std::vector<float>(row_size);
        for (size_t col = 0; col < row_size; col++) {
            row_data[col] = std::stof(row[col]);
        }

        non_default_count +=
            std::count_if(std::next(row_data.cbegin()), row_data.cend(), [&default_value](auto &v) {
                return !MathHelper::equal(v, default_value);
            });

        table.rows.emplace_back(row_data);
    }

    if (non_default_count < 1) {
        notify_warning(fmt::format("{} to {} relative risk file has only default values.",
                                   source_code_str, target_code_str));
        return std::nullopt;
    }

    return table;
}

std::optional<RelativeRiskEntity>
DataManager::get_relative_risk_to_risk_factor(const DiseaseInfo &source, Gender gender,
                                              const core::Identifier &risk_factor_key) const {
    auto diseases_path = index_["diseases"]["path"].get<std::string>();
    auto disease_folder = index_["diseases"]["disease"]["path"].get<std::string>();
    const auto &risk_node = index_["diseases"]["disease"]["relative_risk"];

    auto risk_folder = risk_node["path"].get<std::string>();
    auto file_folder = risk_node["to_risk_factor"]["path"].get<std::string>();
    auto filename = risk_node["to_risk_factor"]["file_name"].get<std::string>();

    auto source_code_str = source.code.to_string();
    disease_folder = replace_string_tokens(disease_folder, {source_code_str});

    auto factor_key_str = risk_factor_key.to_string();
    std::string gender_name = gender == Gender::male ? "male" : "female";
    auto tokens = {gender_name, source_code_str, factor_key_str};
    filename = replace_string_tokens(filename, tokens);
    filename =
        (root_ / diseases_path / disease_folder / risk_folder / file_folder / filename).string();
    if (!std::filesystem::exists(filename)) {
        notify_warning(fmt::format("{} to {} relative risk file not found, disabled.",
                                   source_code_str, factor_key_str));
        return std::nullopt;
    }

    rapidcsv::Document doc(filename);
    auto table = RelativeRiskEntity{.columns = doc.GetColumnNames(), .rows = {}};
    auto row_size = table.columns.size();
    for (size_t i = 0; i < doc.GetRowCount(); i++) {
        auto row = doc.GetRow<std::string>(i);

        auto row_data = std::vector<float>(row_size);
        for (size_t col = 0; col < row_size; col++) {
            row_data[col] = std::stof(row[col]);
        }

        table.rows.emplace_back(row_data);
    }

    table.rows.shrink_to_fit();
    table.columns.shrink_to_fit();
    return table;
}

CancerParameterEntity DataManager::get_disease_parameter(const DiseaseInfo &info,
                                                         const Country &country) const {
    auto disease_path = index_["diseases"]["path"].get<std::string>();
    auto disease_folder = index_["diseases"]["disease"]["path"].get<std::string>();

    const auto &params_node = index_["diseases"]["disease"]["parameters"];
    auto params_folder = params_node["path"].get<std::string>();
    const auto &params_files = params_node["files"];

    auto info_code_str = info.code.to_string();
    disease_folder = replace_string_tokens(disease_folder, {info_code_str});

    auto tokens = std::vector<std::string>{std::to_string(country.code)};
    params_folder = replace_string_tokens(params_folder, tokens);
    auto files_folder = (root_ / disease_path / disease_folder / params_folder);
    if (!std::filesystem::exists(files_folder)) {
        throw std::runtime_error(fmt::format("{}, {} parameters folder: '{}' not found.",
                                             info_code_str, country.name, files_folder.string()));
    }

    auto table = CancerParameterEntity();
    for (const auto &file : params_files.items()) {
        auto file_name = (files_folder / file.value().get<std::string>());
        if (!std::filesystem::exists(file_name)) {
            throw std::runtime_error(fmt::format("{}, {} parameters file: '{}' not found.",
                                                 info_code_str, country.name, file_name.string()));
        }

        auto lookup = std::vector<LookupGenderValue>{};
        rapidcsv::Document doc(file_name.string());
        auto mapping =
            create_fields_index_mapping(doc.GetColumnNames(), {"Time", "Male", "Female"});
        for (size_t i = 0; i < doc.GetRowCount(); i++) {
            auto row = doc.GetRow<std::string>(i);
            lookup.emplace_back(LookupGenderValue{
                .value = std::stoi(row[mapping["Time"]]),
                .male = std::stod(row[mapping["Male"]]),
                .female = std::stod(row[mapping["Female"]]),
            });
        }

        if (file.key() == "distribution") {
            table.prevalence_distribution = lookup;
        } else if (file.key() == "survival_rate") {
            table.survival_rate = lookup;
        } else if (file.key() == "death_weight") {
            table.death_weight = lookup;
        } else {
            throw std::out_of_range(
                fmt::format("Unknown disease parameter file type: {}", file.key()));
        }
    }

    index_["diseases"]["time_year"].get_to(table.at_time);
    return table;
}

} // namespace hgps::input