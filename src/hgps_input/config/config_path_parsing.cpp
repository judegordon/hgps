#include "config_path_parsing.h"

#include "io/json_access.h"
#include "io/json_parser.h"

#include <fmt/format.h>

namespace hgps::input {

void rebase_valid_path(std::filesystem::path &path,
                       const std::filesystem::path &base_dir) {
    try {
        if (path.is_relative()) {
            path = std::filesystem::absolute(base_dir / path);
        }

        if (!std::filesystem::exists(path)) {
            throw std::filesystem::filesystem_error(
                "Path does not exist", path, std::make_error_code(std::errc::no_such_file_or_directory));
        }
    } catch (const std::filesystem::filesystem_error &) {
        throw;
    }
}

bool rebase_valid_path_to(const nlohmann::json &j,
                          const std::string &key,
                          std::filesystem::path &out,
                          const std::filesystem::path &base_dir,
                          hgps::core::InputIssueReport &diagnostics,
                          std::string_view source_path,
                          std::string_view field_path) {
    if (!get_to(j, key, out, diagnostics, source_path, field_path)) {
        return false;
    }

    try {
        rebase_valid_path(out, base_dir);
        return true;
    } catch (const std::filesystem::filesystem_error &) {
        diagnostics.error(
            hgps::core::IssueCode::missing_file,
            {.source_path = std::string{source_path},
             .field_path = join_field_path(field_path, key)},
            fmt::format("Path does not exist: {}", out.string()));
        return false;
    }
}

void rebase_valid_path_to(const nlohmann::json &j,
                          const std::string &key,
                          std::filesystem::path &out,
                          const std::filesystem::path &base_dir,
                          hgps::core::InputIssueReport &diagnostics,
                          bool &success,
                          std::string_view source_path,
                          std::string_view field_path) {
    if (!rebase_valid_path_to(j, key, out, base_dir, diagnostics, source_path, field_path)) {
        success = false;
    }
}

} // namespace hgps::input