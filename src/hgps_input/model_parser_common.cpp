#include "model_parser_common.h"

#include "json_access.h"
#include "json_parser.h"
#include "schema.h"

#include "hgps_core/exception.h"
#include "hgps_core/string_util.h"

#include <fmt/core.h>

#include <fstream>
#include <sstream>
#include <utility>

namespace {

std::string read_stream_to_string(std::istream &is) {
    std::ostringstream buffer;
    buffer << is.rdbuf();
    return buffer.str();
}

} // namespace

namespace hgps::input {

int get_model_schema_version(const std::string &model_name) {
    if (model_name == "ebhlm") {
        return 1;
    }

    if (model_name == "kevinhall") {
        return 2;
    }

    if (model_name == "hlm") {
        return 1;
    }

    if (model_name == "staticlinear") {
        return 2;
    }

    return 0;
}

nlohmann::json load_json(const std::filesystem::path &filepath,
                         hgps::core::Diagnostics &diagnostics,
                         std::string_view field_path) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_file,
                          {.source_path = filepath.string(),
                           .field_path = std::string{field_path}},
                          "Could not open file");
        return {};
    }

    try {
        return nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = filepath.string(),
                           .field_path = std::string{field_path}},
                          e.what());
    } catch (const nlohmann::json::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = filepath.string(),
                           .field_path = std::string{field_path}},
                          e.what());
    }

    return {};
}

std::optional<std::pair<std::string, nlohmann::json>>
load_and_validate_model_json(const std::filesystem::path &model_path,
                             hgps::core::Diagnostics &diagnostics) {
    std::ifstream ifs{model_path};
    if (!ifs.is_open()) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_file,
                          {.source_path = model_path.string()},
                          "Could not open model file");
        return std::nullopt;
    }

    const auto file_contents = read_stream_to_string(ifs);
    if (file_contents.empty()) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = model_path.string()},
                          "Model file is empty");
        return std::nullopt;
    }

    nlohmann::json model_json;
    try {
        model_json = nlohmann::json::parse(file_contents);
    } catch (const nlohmann::json::parse_error &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = model_path.string()},
                          e.what());
        return std::nullopt;
    } catch (const nlohmann::json::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = model_path.string()},
                          e.what());
        return std::nullopt;
    }

    std::string model_name;
    if (!get_to(model_json, "ModelName", model_name, diagnostics, model_path.string())) {
        return std::nullopt;
    }

    model_name = hgps::core::to_lower(model_name);
    const auto schema_version = get_model_schema_version(model_name);
    if (schema_version == 0) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_enum_value,
                          {.source_path = model_path.string(), .field_path = "ModelName"},
                          fmt::format("Unknown model name '{}'", model_name));
        return std::nullopt;
    }

    try {
        std::istringstream validation_stream{file_contents};
        validate_json(validation_stream,
                      fmt::format("config/models/{}.json", model_name),
                      schema_version);
    } catch (const hgps::core::HgpsException &e) {
        diagnostics.error(hgps::core::DiagnosticCode::schema_violation,
                          {.source_path = model_path.string()},
                          e.what());
        return std::nullopt;
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::schema_violation,
                          {.source_path = model_path.string()},
                          e.what());
        return std::nullopt;
    }

    return std::make_optional(std::make_pair(std::move(model_name), std::move(model_json)));
}

hgps::core::Income map_income_category(const std::string &key,
                                       int category_count,
                                       hgps::core::Diagnostics &diagnostics,
                                       std::string_view source_path,
                                       std::string_view field_path) {
    if (hgps::core::case_insensitive::equals(key, "unknown")) {
        return hgps::core::Income::unknown;
    }

    if (hgps::core::case_insensitive::equals(key, "low")) {
        return hgps::core::Income::low;
    }

    if (hgps::core::case_insensitive::equals(key, "high")) {
        return hgps::core::Income::high;
    }

    if (category_count == 3) {
        if (hgps::core::case_insensitive::equals(key, "middle")) {
            return hgps::core::Income::middle;
        }
    } else if (category_count == 4) {
        if (hgps::core::case_insensitive::equals(key, "lowermiddle")) {
            return hgps::core::Income::lowermiddle;
        }

        if (hgps::core::case_insensitive::equals(key, "uppermiddle")) {
            return hgps::core::Income::uppermiddle;
        }
    } else {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = std::string{source_path},
                           .field_path = std::string{field_path}},
                          fmt::format("Unsupported income category count {}", category_count));
        return hgps::core::Income::unknown;
    }

    diagnostics.error(
        hgps::core::DiagnosticCode::invalid_enum_value,
        {.source_path = std::string{source_path}, .field_path = std::string{field_path}},
        fmt::format("Income category '{}' is unrecognised for {} category system",
                    key,
                    category_count));
    return hgps::core::Income::unknown;
}

} // namespace hgps::input