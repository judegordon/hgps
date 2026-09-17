#include "data_source.h"

#include "core/chars.h"
#include "core/string_util.h"
#include "paths.h"
#include "process.h"
#include "sha256.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace hgps::io {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

bool is_url(const std::string &source) {
    return source.starts_with("http://") || source.starts_with("https://");
}

bool is_hex_sha256(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
               return core::chars::is_digit(c) || (core::chars::to_lower(c) >= 'a' &&
                                                   core::chars::to_lower(c) <= 'f');
           });
}

} // namespace

DataSource::DataSource(std::string source, std::optional<std::string> checksum,
                       std::filesystem::path config_directory)
    : source_{std::move(source)}, checksum_{std::move(checksum)},
      config_directory_{std::move(config_directory)} {

    if (is_url(source_)) {
        kind_ = DataSourceKind::url;
        return;
    }

    // A relative path is relative to the config that named it, so a config and its data can move
    // together.
    std::filesystem::path path{source_};
    if (path.is_relative() && !config_directory_.empty()) {
        path = std::filesystem::absolute(config_directory_ / path);
        source_ = path.string();
    }

    kind_ = source_.ends_with(".zip") ? DataSourceKind::zip_archive : DataSourceKind::directory;
}

std::filesystem::path DataSource::cache_directory_for(const std::string &sha256) {
    if (!is_hex_sha256(sha256)) {
        throw std::invalid_argument(
            fmt::format("'{}' is not a SHA-256 hash: 64 hexadecimal characters expected", sha256));
    }

    return cache_directory() / "data" / sha256.substr(0, 2) / sha256.substr(2);
}

std::optional<std::filesystem::path> DataSource::resolve(diag::IssueReport &report) const {
    const IssueLocation where{.field = "/data/source"};

    if (kind_ == DataSourceKind::directory) {
        if (!std::filesystem::is_directory(source_)) {
            report.error(IssueCode::data_source_invalid, where,
                         fmt::format("'{}' is not a directory, a .zip file or an http(s) URL",
                                     source_));
            return std::nullopt;
        }
        return std::filesystem::path{source_};
    }

    // Both remaining kinds end in an archive whose bytes must match a stated hash.
    if (!checksum_.has_value()) {
        report.error(IssueCode::data_checksum_missing, IssueLocation{.field = "/data/checksum"},
                     fmt::format("'{}' is {}, so 'checksum' is required: data that informs model "
                                 "output is not fetched unverified",
                                 source_,
                                 kind_ == DataSourceKind::url ? "a URL" : "an archive"));
        return std::nullopt;
    }

    if (!is_hex_sha256(*checksum_)) {
        report.error(IssueCode::config_bad_value, IssueLocation{.field = "/data/checksum"},
                     fmt::format("'{}' is not a SHA-256 hash: 64 hexadecimal characters expected",
                                 *checksum_));
        return std::nullopt;
    }

    // The checksum is known before the download, so a previously extracted archive needs neither
    // the network nor the archive itself.
    const auto cached = cache_directory_for(*checksum_);
    if (std::filesystem::is_directory(cached)) {
        return cached;
    }

    if (kind_ == DataSourceKind::zip_archive) {
        if (!std::filesystem::is_regular_file(source_)) {
            report.error(IssueCode::data_source_invalid, where,
                         fmt::format("'{}' is not a file", source_));
            return std::nullopt;
        }
        return resolve_archive(source_, report);
    }

    if (!tool_available("curl")) {
        report.error(IssueCode::data_tool_missing, where,
                     fmt::format("'curl' is needed to fetch '{}' but is not on PATH; download the "
                                 "archive yourself and point 'source' at the file or its extracted "
                                 "directory",
                                 source_));
        return std::nullopt;
    }

    const auto download_path = temporary_directory() / fmt::format("{}.zip", *checksum_);
    std::filesystem::remove(download_path);

    const auto download = run_process(
        "curl", {"--fail", "--silent", "--show-error", "--location", "--output",
                 download_path.string(), source_});

    if (!download.succeeded()) {
        report.error(IssueCode::data_download_failed, where,
                     fmt::format("could not download '{}': curl exited {}{}{}", source_,
                                 download.exit_code, download.output.empty() ? "" : " — ",
                                 download.output));
        std::filesystem::remove(download_path);
        return std::nullopt;
    }

    auto result = resolve_archive(download_path, report);
    std::filesystem::remove(download_path);
    return result;
}

std::optional<std::filesystem::path>
DataSource::resolve_archive(const std::filesystem::path &archive,
                            diag::IssueReport &report) const {
    std::string actual;
    try {
        actual = sha256_file(archive);
    } catch (const std::exception &error) {
        report.error(IssueCode::file_unreadable, IssueLocation{.file = archive.string()},
                     error.what());
        return std::nullopt;
    }

    if (actual != core::to_lower(*checksum_)) {
        report.error(IssueCode::data_checksum_mismatch,
                     IssueLocation{.file = archive.string(), .field = "/data/checksum"},
                     fmt::format("the archive's SHA-256 is {} but the config says {}", actual,
                                 *checksum_));
        return std::nullopt;
    }

    if (!tool_available("unzip")) {
        report.error(IssueCode::data_tool_missing, IssueLocation{.file = archive.string()},
                     "'unzip' is needed to extract the data archive but is not on PATH; extract it "
                     "yourself and point 'source' at the directory");
        return std::nullopt;
    }

    const auto destination = cache_directory_for(*checksum_);

    // Extract to a sibling temporary directory and move it into place, so the cache path exists
    // only if extraction finished. A half-extracted cache entry would be indistinguishable from a
    // complete one on the next run.
    const auto staging = destination.parent_path() /
                         fmt::format("{}.partial", destination.filename().string());
    std::filesystem::remove_all(staging);
    std::error_code ec;
    std::filesystem::create_directories(staging, ec);
    if (ec) {
        report.error(IssueCode::data_extract_failed, IssueLocation{.file = staging.string()},
                     fmt::format("could not create the cache directory: {}", ec.message()));
        return std::nullopt;
    }

    const auto extract =
        run_process("unzip", {"-q", "-o", archive.string(), "-d", staging.string()});
    if (!extract.succeeded()) {
        report.error(IssueCode::data_extract_failed, IssueLocation{.file = archive.string()},
                     fmt::format("could not extract the archive: unzip exited {}{}{}",
                                 extract.exit_code, extract.output.empty() ? "" : " — ",
                                 extract.output));
        std::filesystem::remove_all(staging);
        return std::nullopt;
    }

    std::filesystem::rename(staging, destination, ec);
    if (ec) {
        // Another process may have won the race and put an identical tree there first, which is
        // fine: the path is the content's hash.
        if (std::filesystem::is_directory(destination)) {
            std::filesystem::remove_all(staging);
            return destination;
        }

        report.error(IssueCode::data_extract_failed, IssueLocation{.file = destination.string()},
                     fmt::format("could not move the extracted data into the cache: {}",
                                 ec.message()));
        std::filesystem::remove_all(staging);
        return std::nullopt;
    }

    return destination;
}

} // namespace hgps::io
