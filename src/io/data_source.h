#pragma once

#include "diagnostics/issue_report.h"

#include <filesystem>
#include <optional>
#include <string>

namespace hgps::io {

/// @brief What kind of thing a config's `data.source` names.
enum class DataSourceKind {
    directory,
    zip_archive,
    url,
};

/// @brief Where the back-end data store comes from, and how to get a directory out of it.
///
/// Three kinds, as in the baseline: a directory, a local .zip, or an https URL. Two rules are new
/// (docs/decisions/0011-data-fetched-not-vendored.md):
///
///  - a zip or a URL **requires** a SHA-256 checksum, and a mismatch is an error naming both
///    hashes. Unverified model inputs are not acceptable provenance for numbers that inform
///    policy, and the baseline treats `data.checksum` as optional;
///  - extraction is content-addressed at <cache>/data/<sha[0:2]>/<sha[2:]>/, so a verified
///    archive is extracted once and later runs are local.
class DataSource {
  public:
    /// @param source The config's `data.source`, as written.
    /// @param checksum The config's `data.checksum`, if given.
    /// @param config_directory Relative paths are resolved against this, so a config can sit
    ///        beside its data.
    DataSource(std::string source, std::optional<std::string> checksum,
               std::filesystem::path config_directory);

    DataSourceKind kind() const noexcept { return kind_; }
    const std::string &source() const noexcept { return source_; }

    /// @brief Resolves the source to a directory of data, downloading and extracting if needed.
    ///
    /// Reports and returns nullopt rather than throwing: an unreachable URL or a wrong checksum
    /// is the user's problem to see, alongside everything else wrong with their inputs.
    std::optional<std::filesystem::path> resolve(diag::IssueReport &report) const;

    /// @brief The cache directory an archive with this hash extracts to.
    /// @throws std::invalid_argument if the hash is not 64 hexadecimal characters.
    static std::filesystem::path cache_directory_for(const std::string &sha256);

  private:
    std::string source_;
    std::optional<std::string> checksum_;
    std::filesystem::path config_directory_;
    DataSourceKind kind_{DataSourceKind::directory};

    std::optional<std::filesystem::path> resolve_archive(const std::filesystem::path &archive,
                                                         diag::IssueReport &report) const;
};

} // namespace hgps::io
