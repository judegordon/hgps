#include "schema.h"

#include "hgps/program_dirs.h"

#include <fmt/color.h>
#include <fmt/format.h>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

using jsoncons::json;
namespace jsonschema = jsoncons::jsonschema;

constexpr const char *SchemaURLPrefix =
    "https://raw.githubusercontent.com/imperialCHEPI/healthgps/main/schemas/";

json resolve_uri(const std::string &uri_string, const std::filesystem::path &program_directory) {
    if (!uri_string.starts_with(SchemaURLPrefix)) {
        throw std::runtime_error(fmt::format("Unable to load schema URL: {}", uri_string));
    }

    const auto relative_schema_path =
        std::filesystem::path{uri_string.substr(std::string_view{SchemaURLPrefix}.size())};
    const auto local_schema_path = program_directory / "schemas" / relative_schema_path;

    std::ifstream ifs{local_schema_path};
    if (!ifs) {
        throw std::runtime_error(
            fmt::format("Failed to read schema file: {}", local_schema_path.string()));
    }

    return json::parse(ifs);
}

} // namespace

namespace hgps::input {

void validate_json(std::istream &is, const std::string &schema_file_name, int schema_version) {
    (void)schema_version;

    const auto data = json::parse(is);

    const auto program_dir = hgps::get_program_directory();
    const auto local_schema_path = program_dir / "schemas" / schema_file_name;

    std::ifstream ifs_schema{local_schema_path};
    if (!ifs_schema) {
        throw std::runtime_error(
            fmt::format("Failed to load schema: {}", local_schema_path.string()));
    }

    const auto schema_json = json::parse(ifs_schema);
    const auto schema_url = fmt::format("{}{}", SchemaURLPrefix, schema_file_name);

    const auto resolver = [&program_dir](const std::string &uri_string) {
        return resolve_uri(uri_string, program_dir);
    };

    const auto schema = jsonschema::make_json_schema(
        schema_json, schema_url, resolver, jsonschema::evaluation_options{});

    schema.validate(data);
}

nlohmann::json load_and_validate_json(const std::filesystem::path &file_path,
                                      const std::string &schema_file_name, int schema_version,
                                      bool require_schema_property) {
    std::ifstream ifs{file_path};
    if (!ifs) {
        throw std::runtime_error(fmt::format("File not found: {}", file_path.string()));
    }

    auto json_data = nlohmann::json::parse(ifs);
    const auto expected_schema_url = fmt::format("{}{}", SchemaURLPrefix, schema_file_name);

    if (!json_data.contains("$schema")) {
        const auto message =
            fmt::format("File missing $schema property: {}", file_path.string());
        if (require_schema_property) {
            throw std::runtime_error(message);
        }
        fmt::print(fmt::fg(fmt::color::dark_salmon), "{}\n", message);
    } else {
        const auto actual_schema_url = json_data.at("$schema").get<std::string>();
        if (actual_schema_url != expected_schema_url) {
            throw std::runtime_error(fmt::format(
                "Invalid schema URL provided: {} (expected: {})",
                actual_schema_url,
                expected_schema_url));
        }
    }

    ifs.clear();
    ifs.seekg(0);
    validate_json(ifs, schema_file_name, schema_version);

    return json_data;
}

} // namespace hgps::input