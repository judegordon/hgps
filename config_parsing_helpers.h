#pragma once

#include "configuration.h"
#include "poco.h"
#include "hgps_core/diagnostics.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace hgps::input {

nlohmann::json get(const nlohmann::json &j, const std::string &key);

inline std::string join_field_path(std::string_view parent, std::string_view key) {
    if (parent.empty()) {
        return std::string{key};
    }

    std::string path;
    path.reserve(parent.size() + 1 + key.size());
    path.append(parent);
    path.push_back('.');
    path.append(key);
    return path;
}

template <typename T>
bool get_to(const nlohmann::json &j, const std::string &key, T &out,
            hgps::core::Diagnostics &diagnostics,
            std::string_view source_path = {},
            std::string_view field_path = {}) noexcept {
    const auto full_field_path = join_field_path(field_path, key);

    try {
        out = j.at(key).get<T>();
        return true;
    } catch (const nlohmann::json::out_of_range &) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = std::move(full_field_path)},
                          "Missing required key");
        return false;
    } catch (const nlohmann::json::type_error &) {
        diagnostics.error(hgps::core::DiagnosticCode::wrong_type,
                          {.source_path = std::string{source_path},
                           .field_path = std::move(full_field_path)},
                          "Key has wrong type");
        return false;
    } catch (const nlohmann::json::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = std::string{source_path},
                           .field_path = std::move(full_field_path)},
                          e.what());
        return false;
    }
}

template <typename T>
bool get_to(const nlohmann::json &j, const std::string &key, T &out,
            hgps::core::Diagnostics &diagnostics, bool &success,
            std::string_view source_path = {},
            std::string_view field_path = {}) noexcept {
    const bool ok = get_to(j, key, out, diagnostics, source_path, field_path);
    if (!ok) {
        success = false;
    }
    return ok;
}

void rebase_valid_path(std::filesystem::path &path, const std::filesystem::path &base_dir);

bool rebase_valid_path_to(const nlohmann::json &j, const std::string &key,
                          std::filesystem::path &out,
                          const std::filesystem::path &base_dir,
                          hgps::core::Diagnostics &diagnostics,
                          std::string_view source_path = {},
                          std::string_view field_path = {}) noexcept;

void rebase_valid_path_to(const nlohmann::json &j, const std::string &key,
                          std::filesystem::path &out,
                          const std::filesystem::path &base_dir,
                          hgps::core::Diagnostics &diagnostics,
                          bool &success,
                          std::string_view source_path = {},
                          std::string_view field_path = {}) noexcept;

FileInfo get_file_info(const nlohmann::json &node, const std::filesystem::path &base_dir,
                       hgps::core::Diagnostics &diagnostics,
                       std::string_view source_path = {},
                       std::string_view field_path = {});

SettingsInfo get_settings(const nlohmann::json &j,
                          hgps::core::Diagnostics &diagnostics,
                          std::string_view source_path = {},
                          std::string_view field_path = {});

BaselineInfo get_baseline_info(const nlohmann::json &j, const std::filesystem::path &base_dir,
                               hgps::core::Diagnostics &diagnostics,
                               std::string_view source_path = {},
                               std::string_view field_path = {});

void load_interventions(const nlohmann::json &running, Configuration &config,
                        hgps::core::Diagnostics &diagnostics,
                        std::string_view source_path = {},
                        std::string_view field_path = {});

} // namespace hgps::input