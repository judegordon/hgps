#include "manifest.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <stdexcept>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace hgps::engine {

std::string utc_timestamp_now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    // gmtime_r rather than gmtime: the latter returns a pointer into a shared static, and the
    // library is callable from a host that has other threads of its own.
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif

    return fmt::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", parts.tm_year + 1900,
                       parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec);
}

void write_manifest(const std::filesystem::path &path, const Manifest &manifest) {
    nlohmann::json document;

    // Ordered by what a reader asks first: which run is this, then what produced it.
    document["manifest_version"] = 1;

    document["config"] = {{"path", manifest.config_path.string()},
                          {"sha256", manifest.config_sha256}};

    document["data"] = {{"source", manifest.data_source},
                        {"directory", manifest.data_directory.string()}};
    // A plain directory has no single hash, and saying so is not the same as leaving the field out.
    if (manifest.data_checksum.empty()) {
        document["data"]["checksum"] = nullptr;
        document["data"]["checksum_note"] =
            "the data source is a directory, which has no single archive hash";
    } else {
        document["data"]["checksum"] = manifest.data_checksum;
    }

    document["seed"] = manifest.seed;
    document["run_seeds"] = manifest.run_seeds;

    document["engine"] = {{"version", manifest.engine_version},
                          {"git_commit", manifest.git_commit},
                          {"git_describe", manifest.git_describe},
                          {"git_dirty", manifest.git_dirty},
                          {"platform", manifest.platform},
                          {"compiler", manifest.compiler},
                          {"build_type", manifest.build_type}};

    document["timing"] = {{"started_utc", manifest.started_utc},
                          {"finished_utc", manifest.finished_utc},
                          {"elapsed_ms", manifest.elapsed_ms}};

    document["scenarios"] = manifest.scenarios;

    document["run"] = {{"country", manifest.country},
                       {"start_time", manifest.start_time},
                       {"stop_time", manifest.stop_time},
                       {"trial_runs", manifest.trial_runs},
                       {"cohort_size", manifest.cohort_size},
                       {"threads", manifest.threads},
                       {"years_completed", manifest.years_completed},
                       {"cancelled", manifest.cancelled}};

    document["results"] = manifest.results;

    // Present and null in an ordinary run rather than absent, so "was this output perturbed?" is
    // answered by every manifest instead of by the absence of a key.
    if (manifest.perturbation.empty()) {
        document["perturbation"] = nullptr;
    } else {
        document["perturbation"] = manifest.perturbation;
    }

    std::ofstream stream{path, std::ios::trunc};
    if (!stream) {
        throw std::runtime_error(
            fmt::format("could not open the run manifest for writing: {}", path.string()));
    }
    stream << document.dump(2) << '\n';
    if (!stream) {
        throw std::runtime_error(fmt::format("could not write the run manifest: {}", path.string()));
    }
}

} // namespace hgps::engine
