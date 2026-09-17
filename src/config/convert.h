#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hgps::config {

/// @brief What the converter did, so the output is auditable rather than magic.
struct ConversionNote {
    enum class Level { info, warning, error };

    Level level{Level::info};
    std::string message;
};

struct ConversionResult {
    std::optional<nlohmann::json> document;
    std::vector<ConversionNote> notes;

    bool succeeded() const {
        return document.has_value() &&
               std::none_of(notes.begin(), notes.end(), [](const ConversionNote &note) {
                   return note.level == ConversionNote::Level::error;
               });
    }
};

/// @brief Converts an upstream Health-GPS v1 config into this repository's config v2.
///
/// Handles both upstream variants: a legacy `config.json`, which has no `project_requirements`
/// and may carry the deprecated `trend_type` and `income_categories` fields, and a
/// `new_config.json`, which has the block. Where the input lacks `project_requirements`, the
/// converter looks for a `new_config.json` beside it and takes the block from there; failing
/// that it derives what it can from the legacy fields and fills the rest from the documented
/// defaults, saying which route it took for each.
///
/// docs/decisions/0010-config-v2-and-a-converter.md.
///
/// @param document The parsed upstream config.
/// @param source_path The input's path, used to find a sibling new_config.json.
ConversionResult convert_config(const nlohmann::json &document,
                                const std::filesystem::path &source_path);

/// @brief Rewrites the config's relative input paths so they still name the same files.
///
/// A converted config lives in this repository while the model CSVs and JSONs it names stay
/// upstream: the upstream examples are read-only, and their data files are not ours to copy.
/// Config v2 resolves a relative input path against the config's own directory, so moving the
/// config means rewriting those paths — every one of them, or the config silently names a file
/// that does not exist.
///
/// Only input paths move. `output.folder` and `output.file_name` are the user's choice of where
/// results go and are left exactly as written.
///
/// @param document The converted config, modified in place.
/// @param from_directory The directory the paths were relative to (the upstream example).
/// @param to_directory The directory the converted config will live in.
void rebase_input_paths(nlohmann::json &document, const std::filesystem::path &from_directory,
                        const std::filesystem::path &to_directory,
                        std::vector<ConversionNote> &notes);

} // namespace hgps::config
