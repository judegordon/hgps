#include "config_section_parsing.h"

#include "config_path_parsing.h"
#include "json_access.h"
#include "jsonparser.h"

#include <unordered_map>

namespace hgps::input {

using json = nlohmann::json;

std::optional<FileInfo> get_file_info(const json &node,
                                      const std::filesystem::path &base_dir,
                                      hgps::core::Diagnostics &diagnostics,
                                      std::string_view source_path,
                                      std::string_view field_path) {
    bool success = true;
    FileInfo info{};

    rebase_valid_path_to(node, "name", info.name, base_dir, diagnostics, success,
                         source_path, field_path);

    get_to(node, "format", info.format, diagnostics, success,
           source_path, field_path);

    get_to(node, "delimiter", info.delimiter, diagnostics, success,
           source_path, field_path);

    get_to(node, "columns", info.columns, diagnostics, success,
           source_path, field_path);

    if (!success) {
        return std::nullopt;
    }

    return info;
}

std::optional<SettingsInfo> get_settings(const json &j,
                                         hgps::core::Diagnostics &diagnostics,
                                         std::string_view source_path,
                                         std::string_view field_path) {
    SettingsInfo info{};

    if (!get_to(j, "settings", info, diagnostics, source_path, field_path)) {
        return std::nullopt;
    }

    return info;
}

std::optional<BaselineInfo> get_baseline_info(const json &j,
                                              const std::filesystem::path &base_dir,
                                              hgps::core::Diagnostics &diagnostics,
                                              std::string_view source_path,
                                              std::string_view field_path) {
    const auto adj = get(j, "baseline_adjustments", diagnostics, source_path, field_path);

    bool success = true;
    BaselineInfo info{};

    get_to(adj, "format", info.format, diagnostics, success,
           source_path, "modelling.baseline_adjustments");

    get_to(adj, "delimiter", info.delimiter, diagnostics, success,
           source_path, "modelling.baseline_adjustments");

    get_to(adj, "encoding", info.encoding, diagnostics, success,
           source_path, "modelling.baseline_adjustments");

    if (get_to(adj, "file_names", info.file_names, diagnostics, success,
               source_path, "modelling.baseline_adjustments")) {
        for (auto &[name, path] : info.file_names) {
            try {
                rebase_valid_path(path, base_dir);
            } catch (const std::filesystem::filesystem_error &) {
                diagnostics.error(
                    hgps::core::DiagnosticCode::missing_file,
                    {.source_path = std::string{source_path},
                     .field_path = join_field_path("modelling.baseline_adjustments.file_names", name)},
                    "Referenced file does not exist");
                success = false;
            }
        }
    }

    if (!success) {
        return std::nullopt;
    }

    return info;
}

void load_interventions(const json &running,
                        Configuration &config,
                        hgps::core::Diagnostics &diagnostics,
                        std::string_view source_path,
                        std::string_view field_path) {
    const auto interventions = get(running, "interventions", diagnostics, source_path, field_path);

    bool success = true;

    std::optional<hgps::core::Identifier> active_type_id;
    get_to(interventions, "active_type_id", active_type_id, diagnostics, success,
           source_path, "running.interventions");

    std::unordered_map<hgps::core::Identifier, PolicyScenarioInfo> policy_types;
    get_to(interventions, "types", policy_types, diagnostics, success,
           source_path, "running.interventions");

    if (active_type_id) {
        const auto it = policy_types.find(*active_type_id);
        if (it == policy_types.end()) {
            diagnostics.error(
                hgps::core::DiagnosticCode::invalid_value,
                {.source_path = std::string{source_path},
                 .field_path = "running.interventions.active_type_id"},
                "Unknown active intervention type identifier");
            success = false;
        } else {
            config.active_intervention = it->second;
            config.active_intervention->identifier = active_type_id->to_string();
        }
    }

    (void)success;
}

} // namespace hgps::input