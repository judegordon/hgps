#pragma once
#include "configuration.h"
#include "poco.h"

#include <fmt/color.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace hgps::input {
nlohmann::json get(const nlohmann::json &j, const std::string &key);

template <class T> bool get_to(const nlohmann::json &j, const std::string &key, T &out) noexcept {
    try {
        out = j.at(key).get<T>();
        return true;
    } catch (const nlohmann::json::out_of_range &) {
        fmt::print(fg(fmt::color::red), "Missing key \"{}\"\n", key);
        return false;
    } catch (const nlohmann::json::type_error &) {
        fmt::print(fg(fmt::color::red), "Key \"{}\" is of wrong type\n", key);
        return false;
    }
}

template <class T>
bool get_to(const nlohmann::json &j, const std::string &key, T &out, bool &success) noexcept {
    const bool ret = get_to(j, key, out);
    if (!ret) {
        success = false;
    }
    return ret;
}

void rebase_valid_path(std::filesystem::path &path, const std::filesystem::path &base_dir);

bool rebase_valid_path_to(const nlohmann::json &j, const std::string &key,
                          std::filesystem::path &out,
                          const std::filesystem::path &base_dir) noexcept;

void rebase_valid_path_to(const nlohmann::json &j, const std::string &key,
                          std::filesystem::path &out, const std::filesystem::path &base_dir,
                          bool &success) noexcept;

FileInfo get_file_info(const nlohmann::json &node, const std::filesystem::path &base_dir);

SettingsInfo get_settings(const nlohmann::json &j);

BaselineInfo get_baseline_info(const nlohmann::json &j, const std::filesystem::path &base_dir);

void load_interventions(const nlohmann::json &running, Configuration &config);
} // namespace hgps::input
