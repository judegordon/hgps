// The run manifest: what produced a set of result files.
//
// A result CSV with no record of the config, data, seed and binary behind it is a table of numbers,
// not evidence. The baseline's result JSON carries some of that; the manifest carries all of it,
// in one file per run, beside the results
// (docs/decisions/0034-a-run-manifest-beside-the-results.md).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hgps::engine {

/// @brief Everything the manifest records. Assembled by the engine; nothing here is guessed.
struct Manifest {
    /// @brief The config file, as the caller named it, and the SHA-256 of its bytes.
    std::filesystem::path config_path;
    std::string config_sha256;

    /// @brief `data.source` as written, and the verified archive hash — empty for a plain
    ///        directory, which has no single hash, and that emptiness is recorded as such.
    std::string data_source;
    std::string data_checksum;

    /// @brief The directory the data was finally read from, after any download and extraction.
    std::filesystem::path data_directory;

    /// @brief The master seed actually used, and each trial run's derived seed.
    std::uint32_t seed{};
    std::vector<std::uint32_t> run_seeds;

    std::string engine_version;
    std::string git_commit;
    std::string git_describe;
    bool git_dirty{false};
    std::string platform;
    std::string compiler;
    std::string build_type;

    /// @brief ISO-8601 UTC, to the second.
    std::string started_utc;
    std::string finished_utc;

    double elapsed_ms{};
    bool cancelled{false};

    /// @brief The scenarios that ran, in the order they ran.
    std::vector<std::string> scenarios;

    std::string country;
    int start_time{};
    int stop_time{};
    unsigned int trial_runs{};
    std::size_t cohort_size{};
    std::size_t threads{1};
    std::size_t years_completed{};

    /// @brief The result files, relative to the manifest's own directory when they sit beside it.
    std::vector<std::string> results;

    /// @brief Empty in every ordinary run; the perturbation specification otherwise, so a
    ///        deliberately wrong run cannot be mistaken for a real one (ADR 0036).
    std::string perturbation;

    /// @brief The deliberate deviations this run put back, by name, or empty for none — which is
    ///        every ordinary run. A result produced with a flag on is reproducing a baseline
    ///        defect on purpose, and the manifest is where that is recorded
    ///        (ADR 0041, docs/deviations.md).
    std::vector<std::string> baseline_compat;
};

/// @brief ISO-8601 UTC "2026-09-18T04:53:12Z" for now.
std::string utc_timestamp_now();

/// @brief Writes the manifest as JSON.
/// @throws std::runtime_error if the file cannot be opened.
void write_manifest(const std::filesystem::path &path, const Manifest &manifest);

} // namespace hgps::engine
