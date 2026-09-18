#include "json_view.h"

#include <fstream>
#include <stdexcept>

#include <fmt/format.h>

namespace hgps::server {
namespace {

/// @brief A location, with its absent fields absent rather than present and empty.
nlohmann::json location_of(const api::Location &location) {
    nlohmann::json out = nlohmann::json::object();
    if (!location.file.empty()) {
        out["file"] = location.file;
    }
    if (!location.field.empty()) {
        out["pointer"] = location.field;
    }
    if (location.line.has_value()) {
        out["line"] = *location.line;
    }
    if (location.column.has_value()) {
        out["column"] = *location.column;
    }
    return out;
}

} // namespace

nlohmann::json to_json(const api::Diagnostic &diagnostic) {
    return {
        {"severity", std::string{api::to_string(diagnostic.severity)}},
        {"code", diagnostic.code},
        {"message", diagnostic.message},
        {"location", location_of(diagnostic.location)},
    };
}

nlohmann::json to_json(const api::Report &report) {
    auto out = nlohmann::json::array();
    for (const auto &diagnostic : report.diagnostics()) {
        out.push_back(to_json(diagnostic));
    }
    return out;
}

nlohmann::json summary_of(const api::Configuration &configuration) {
    nlohmann::json out{
        {"sha256", configuration.sha256()},
        {"seed", configuration.seed()},
        {"start_time", configuration.start_time()},
        {"stop_time", configuration.stop_time()},
        {"trial_runs", configuration.trial_runs()},
        {"diseases", configuration.diseases()},
        {"data_source", configuration.data_source()},
        {"output_folder", configuration.output_folder().string()},
        {"output_file_name", configuration.output_file_name()},
        {"baseline_compat", configuration.baseline_compat().names()},
    };

    // Present and null rather than absent: "is an intervention active?" is a question every
    // configuration answers, and a missing key is not the same as the answer "no".
    const auto intervention = configuration.active_intervention();
    out["active_intervention"] = intervention.has_value() ? nlohmann::json(*intervention)
                                                          : nlohmann::json(nullptr);
    const auto checksum = configuration.data_checksum();
    out["data_checksum"] = checksum.has_value() ? nlohmann::json(*checksum) : nlohmann::json(nullptr);
    return out;
}

nlohmann::json to_json(const api::Run::Description &description) {
    return {
        {"country", description.country},
        {"disease_count", description.disease_count},
        {"risk_factor_count", description.risk_factor_count},
        {"cohort_size", description.cohort_size},
        {"start_time", description.start_time},
        {"stop_time", description.stop_time},
        {"trial_runs", description.trial_runs},
        {"seed", description.seed},
        {"run_seeds", description.run_seeds},
        {"scenarios", description.scenarios},
    };
}

nlohmann::json version_document(int api_version) {
    const auto &info = api::build_info();

    auto flags = nlohmann::json::array();
    for (const auto flag : api::BaselineCompat::known()) {
        flags.push_back({{"name", std::string{api::BaselineCompat::name_of(flag)}},
                         {"description", std::string{api::BaselineCompat::description_of(flag)}}});
    }

    return {
        {"engine_version", std::string{info.version}},
        {"git_commit", std::string{info.git_commit}},
        {"git_describe", std::string{info.git_describe}},
        {"git_dirty", info.git_dirty},
        {"platform", std::string{info.platform}},
        {"compiler", std::string{info.compiler}},
        {"build_type", std::string{info.build_type}},
        {"api_version", api_version},
        {"baseline_compat_flags", flags},
    };
}

nlohmann::json error_document(const std::string &code, const std::string &message,
                              const api::Report *report) {
    nlohmann::json error{{"code", code}, {"message", message}};
    if (report != nullptr && !report->diagnostics().empty()) {
        error["diagnostics"] = to_json(*report);
    }
    return {{"error", error}};
}

namespace {

/// @brief Replaces every `{"$ref": "relative/path.json"}` with the document it names.
///
/// Only relative file references, which is all schemas/v2 uses: a `$ref` with a fragment or a
/// scheme is left alone and would be a different thing to resolve. A cycle would be an infinite
/// loop, so the chain of files being resolved is carried and a repeat is an error rather than a
/// hang — schemas/v2 has no cycle today and a future one should fail loudly.
nlohmann::json resolve(const nlohmann::json &node, const std::filesystem::path &directory,
                       std::vector<std::filesystem::path> &open);

nlohmann::json load_and_resolve(const std::filesystem::path &path,
                                std::vector<std::filesystem::path> &open) {
    const auto canonical = std::filesystem::weakly_canonical(path);
    for (const auto &already : open) {
        if (already == canonical) {
            throw std::runtime_error(
                fmt::format("the config schema references itself through {}", path.string()));
        }
    }

    std::ifstream stream{path};
    if (!stream) {
        throw std::runtime_error(fmt::format("the config schema names {}, which is not there; a "
                                             "half-resolved schema would generate a form with "
                                             "silently missing fields",
                                             path.string()));
    }

    nlohmann::json document;
    stream >> document;

    open.push_back(canonical);
    auto resolved = resolve(document, path.parent_path(), open);
    open.pop_back();
    return resolved;
}

nlohmann::json resolve(const nlohmann::json &node, const std::filesystem::path &directory,
                       std::vector<std::filesystem::path> &open) {
    if (node.is_array()) {
        auto out = nlohmann::json::array();
        for (const auto &element : node) {
            out.push_back(resolve(element, directory, open));
        }
        return out;
    }
    if (!node.is_object()) {
        return node;
    }

    if (const auto reference = node.find("$ref");
        reference != node.end() && reference->is_string()) {
        const auto target = reference->get<std::string>();
        if (!target.empty() && target.find("://") == std::string::npos && target.front() != '#') {
            auto inlined = load_and_resolve(directory / target, open);
            // Anything beside the $ref — a description, usually — stays, and wins, because it is
            // the more specific statement about this use of the schema.
            for (const auto &[key, value] : node.items()) {
                if (key != "$ref") {
                    inlined[key] = resolve(value, directory, open);
                }
            }
            return inlined;
        }
    }

    auto out = nlohmann::json::object();
    for (const auto &[key, value] : node.items()) {
        out[key] = resolve(value, directory, open);
    }
    return out;
}

} // namespace

nlohmann::json inline_schema_refs(const std::filesystem::path &schema_path) {
    std::vector<std::filesystem::path> open;
    return load_and_resolve(schema_path, open);
}

} // namespace hgps::server
