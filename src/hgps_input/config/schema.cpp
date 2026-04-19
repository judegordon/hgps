#include "schema.h"

#include "hgps/utils/program_dirs.h"

#include <fmt/color.h>
#include <fmt/format.h>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace jsonschema = jsoncons::jsonschema;

constexpr const char *SchemaPathMarker = "/schemas/";
constexpr const char *DefaultSchemaURLPrefix =
    "https://raw.githubusercontent.com/imperialCHEPI/healthgps/main/schemas/";

std::string get_schema_base_url(const std::string &schema_url) {
    const auto pos = schema_url.find(SchemaPathMarker);
    if (pos == std::string::npos) {
        throw std::runtime_error(fmt::format(
            "Schema URL does not contain '{}': {}", SchemaPathMarker, schema_url));
    }

    return schema_url.substr(0, pos + std::string_view{SchemaPathMarker}.size());
}

jsoncons::json resolve_uri(const jsoncons::uri &uri,
                           const std::filesystem::path &program_directory) {
    const auto uri_string = uri.string();
    const auto base_url = get_schema_base_url(uri_string);

    const auto relative_schema_path =
        std::filesystem::path{uri_string.substr(base_url.size())};
    const auto local_schema_path = program_directory / "schemas" / relative_schema_path;

    std::ifstream ifs{local_schema_path};
    if (!ifs) {
        throw std::runtime_error(
            fmt::format("Failed to read schema file: {}", local_schema_path.string()));
    }

    return jsoncons::json::parse(ifs);
}

} // namespace

namespace hgps::input {

void validate_json(std::istream &is, const std::string &schema_file_name, int schema_version) {
    (void)schema_version;

    const auto data = jsoncons::json::parse(is);

    const auto program_dir = hgps::get_program_directory();
    const auto local_schema_path = program_dir / "schemas" / schema_file_name;

    std::ifstream ifs_schema{local_schema_path};
    if (!ifs_schema) {
        throw std::runtime_error(
            fmt::format("Failed to load schema: {}", local_schema_path.string()));
    }

    const auto schema_json = jsoncons::json::parse(ifs_schema);
    const auto schema_url = fmt::format("{}{}", DefaultSchemaURLPrefix, schema_file_name);

    const auto resolver = [&program_dir](const jsoncons::uri &uri) {
        return resolve_uri(uri, program_dir);
    };

    const auto schema = jsonschema::make_json_schema(
        schema_json, schema_url, resolver, jsonschema::evaluation_options{});

    schema.validate(data);
}

nlohmann::json load_and_validate_json(const std::filesystem::path &file_path,
                                      const std::string &schema_file_name, int schema_version,
                                      bool require_schema_property) {
    std::cout << "Loading JSON: " << file_path << std::endl;

    std::ifstream ifs{file_path};
    if (!ifs) {
        throw std::runtime_error(fmt::format("File not found: {}", file_path.string()));
    }

    auto json_data = nlohmann::json::parse(ifs);

    if (!json_data.contains("$schema")) {
        const auto message = fmt::format("File missing $schema property: {}", file_path.string());
        if (require_schema_property) {
            throw std::runtime_error(message);
        }
        fmt::print(fmt::fg(fmt::color::dark_salmon), "{}\n", message);
    } else {
        const auto actual_schema_url = json_data.at("$schema").get<std::string>();
        const auto expected_suffix = std::string{SchemaPathMarker} + schema_file_name;

        if (!actual_schema_url.ends_with(expected_suffix)) {
            throw std::runtime_error(fmt::format(
                "Invalid schema URL provided: {} (expected URL ending with: {})",
                actual_schema_url,
                expected_suffix));
        }
    }

    ifs.clear();
    ifs.seekg(0);
    validate_json(ifs, schema_file_name, schema_version);

    return json_data;
}

} // namespace hgps::input