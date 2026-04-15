#pragma once

#include "hgps_core/diagnostics/input_issue.h"

#include <nlohmann/json.hpp>

#include <string>
#include "json_parser.h"
#include <string_view>

namespace hgps::input {

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

inline nlohmann::json get(const nlohmann::json &j, const std::string &key,
                          hgps::core::InputIssueReport &diagnostics,
                          std::string_view source_path = {},
                          std::string_view field_path = {}) {
    const auto full_field_path = join_field_path(field_path, key);

    try {
        return j.at(key);
    } catch (const nlohmann::json::out_of_range &) {
        diagnostics.error(hgps::core::IssueCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = full_field_path},
                          "Missing required key");
    } catch (const nlohmann::json::type_error &) {
        diagnostics.error(hgps::core::IssueCode::wrong_type,
                          {.source_path = std::string{source_path},
                           .field_path = full_field_path},
                          "Key has wrong type");
    } catch (const nlohmann::json::exception &e) {
        diagnostics.error(hgps::core::IssueCode::parse_failure,
                          {.source_path = std::string{source_path},
                           .field_path = full_field_path},
                          e.what());
    }

    return {};
}

template <typename T>
bool get_to(const nlohmann::json &j, const std::string &key, T &out,
            hgps::core::InputIssueReport &diagnostics,
            std::string_view source_path = {},
            std::string_view field_path = {}) {
    const auto full_field_path = join_field_path(field_path, key);

    try {
        out = j.at(key).get<T>();
        return true;
    } catch (const nlohmann::json::out_of_range &) {
        diagnostics.error(hgps::core::IssueCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = full_field_path},
                          "Missing required key");
        return false;
    } catch (const nlohmann::json::type_error &) {
        diagnostics.error(hgps::core::IssueCode::wrong_type,
                          {.source_path = std::string{source_path},
                           .field_path = full_field_path},
                          "Key has wrong type");
        return false;
    } catch (const nlohmann::json::exception &e) {
        diagnostics.error(hgps::core::IssueCode::parse_failure,
                          {.source_path = std::string{source_path},
                           .field_path = full_field_path},
                          e.what());
        return false;
    }
}

template <typename T>
bool get_to(const nlohmann::json &j, const std::string &key, T &out,
            hgps::core::InputIssueReport &diagnostics, bool &success,
            std::string_view source_path = {},
            std::string_view field_path = {}) {
    const bool ok = get_to(j, key, out, diagnostics, source_path, field_path);
    if (!ok) {
        success = false;
    }
    return ok;
}

} // namespace hgps::input