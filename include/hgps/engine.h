// The public API of hgps::engine: load a config, resolve its data, build a run, execute it.
//
// This is the whole surface. Nothing here includes an internal header, and the CLI in src/app/
// includes nothing outside include/hgps/ — a test enforces that, because a boundary nobody checks
// is a boundary that leaks (docs/api.md, docs/decisions/0032-library-and-a-thin-cli.md).
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
#pragma once

#include "cancellation.h"
#include "diagnostics.h"
#include "events.h"
#include "version.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hgps::api {

/// @brief What a caller can supply or override when loading a configuration.
struct LoadOptions {
    /// @brief Overrides `output.folder`, and is allowed only when the config leaves it empty.
    ///        Giving a folder in both places is a diagnostic rather than a silent precedence rule.
    ///        This is what a command line's `--output` maps to.
    std::optional<std::string> output_folder;

    /// @brief Replaces `output.folder` whatever the configuration says, with no diagnostic.
    ///
    /// `output_folder` above is command-line ergonomics: somebody who typed `--output` and also
    /// wrote a folder in the file has made a mistake worth telling them about. A host that keeps
    /// results in a directory of its own choosing — a file dialogue's answer, a job directory —
    /// has not, and needs the configuration's value to simply not apply. Setting both is an error.
    std::optional<std::string> output_folder_override;

    /// @brief An HPC array job identifier; 0 means "not a job".
    int job_id{0};

    /// @brief Report more about what was loaded.
    bool verbose{false};

    /// @brief Whether a path named in the configuration must already exist. A caller validating a
    ///        document it has not yet written files for turns this off.
    bool require_files_exist{true};
};

/// @brief A loaded, fully validated configuration.
///
/// Opaque: the configuration's internal shape is this project's business and changes between
/// versions, so the accessors below are what a caller can rely on. Move-only, because it owns the
/// validated document and two copies of one configuration would be two runs' worth of confusion.
class Configuration {
  public:
    Configuration(Configuration &&) noexcept;
    Configuration &operator=(Configuration &&) noexcept;
    Configuration(const Configuration &) = delete;
    Configuration &operator=(const Configuration &) = delete;
    ~Configuration();

    /// @brief The file this was loaded from.
    const std::filesystem::path &path() const noexcept;

    /// @brief The SHA-256 of that file's bytes, which is what identifies the scenario in a
    ///        manifest and in an equivalence reference.
    const std::string &sha256() const noexcept;

    /// @brief The master seed the run will use.
    std::uint32_t seed() const noexcept;

    int start_time() const noexcept;
    int stop_time() const noexcept;
    unsigned int trial_runs() const noexcept;

    /// @brief The selected diseases, in the order the configuration lists them.
    std::vector<std::string> diseases() const;

    /// @brief The active intervention's identifier, or nullopt for a baseline-only run.
    std::optional<std::string> active_intervention() const;

    /// @brief Where results will be written.
    std::filesystem::path output_folder() const;

    /// @brief The result file's base name, with `{TIMESTAMP}` and `{JOBID}` already expanded.
    std::string output_file_name() const;

    /// @brief `data.source` as written, and `data.checksum` if the configuration gave one.
    const std::string &data_source() const noexcept;
    std::optional<std::string> data_checksum() const;

    // Internals. Not part of the API; declared here because the implementation needs a handle.
    class Impl;
    const Impl &impl() const noexcept { return *impl_; }
    Impl &impl() noexcept { return *impl_; }
    explicit Configuration(std::unique_ptr<Impl> impl) noexcept;

  private:
    std::unique_ptr<Impl> impl_;
};

/// @brief An opened back-end data store: a directory of tables whose index and disease registry
///        have been validated.
///
/// Resolving may download and extract an archive, which is why it is a separate step from loading
/// a configuration: a caller that only wants to validate a document should not have to wait for a
/// fetch, and a caller with a progress indicator wants to know which step it is on.
class DataHandle {
  public:
    DataHandle(DataHandle &&) noexcept;
    DataHandle &operator=(DataHandle &&) noexcept;
    DataHandle(const DataHandle &) = delete;
    DataHandle &operator=(const DataHandle &) = delete;
    ~DataHandle();

    /// @brief The directory the store was read from, after any download and extraction.
    const std::filesystem::path &directory() const noexcept;

    /// @brief The verified SHA-256 of the archive this came from, or nullopt for a plain
    ///        directory, which has no single hash.
    std::optional<std::string> checksum() const;

    class Impl;
    const Impl &impl() const noexcept { return *impl_; }
    explicit DataHandle(std::unique_ptr<Impl> impl) noexcept;

  private:
    std::unique_ptr<Impl> impl_;
};

/// @brief A run that is ready to execute: every input loaded, every model built, nothing sampled.
///
/// Building a run does all the work that can fail on bad input, so `execute()` fails only for a
/// broken invariant or a file it cannot write. That is the point of the split: a caller can offer
/// "validate" as a button that finishes in a second.
class Run {
  public:
    /// @brief What the run will do, for a caller that wants to show it before starting.
    struct Description {
        std::string country;
        std::size_t disease_count{};
        std::size_t risk_factor_count{};
        std::size_t cohort_size{};
        int start_time{};
        int stop_time{};
        unsigned int trial_runs{};
        std::uint32_t seed{};

        /// @brief Each trial run's derived seed, so a single run can be reproduced on its own.
        std::vector<std::uint32_t> run_seeds;

        /// @brief The scenario names, in the order they will run.
        std::vector<std::string> scenarios;
    };

    Run(Run &&) noexcept;
    Run &operator=(Run &&) noexcept;
    Run(const Run &) = delete;
    Run &operator=(const Run &) = delete;
    ~Run();

    const Description &description() const noexcept;

    class Impl;
    Impl &impl() noexcept { return *impl_; }
    explicit Run(std::unique_ptr<Impl> impl) noexcept;

  private:
    std::unique_ptr<Impl> impl_;
};

/// @brief How to execute a run.
struct RunOptions {
    /// @brief Workers for the parallel sections that draw no randomness. The results are
    ///        byte-identical at any count; if they are not, that is a bug
    ///        (docs/design.md section 4).
    std::size_t threads{1};

    /// @brief Write the run manifest beside the results. On by default, because a result file with
    ///        no record of what produced it is not evidence.
    bool write_manifest{true};

    /// @brief Test-only: corrupt named output channels, to check that the equivalence harness fails
    ///        where it should.
    ///
    /// Empty in every ordinary run, and this is the only field on this API whose purpose is to make a
    /// test fail — a cost paid knowingly
    /// ([ADR 0036](decisions/0036-the-harness-is-tested-against-itself.md)).
    ///
    /// The form is `channel=op:value`, separated by `;`, with `scale` (multiply every age band) and
    /// `step` (add to one band) as the operations:
    ///
    ///     mean_bmi=scale:1.01;mean_energy=scale:1.05;emigrations=step:1
    ///
    /// A specification that does not parse, or that names a channel the output does not have, makes
    /// the run **fail** rather than quietly doing nothing — because a silently unperturbed run would
    /// make the test that uses this pass for the wrong reason. Every run manifest records whatever was
    /// set here, and `null` when nothing was, so no output that came out of this can be mistaken for
    /// a real one (docs/equivalence-method.md §7.2).
    std::string perturbation;
};

/// @brief What a run produced.
struct RunSummary {
    bool succeeded{false};

    /// @brief True if the run stopped early because its cancellation token was set. A cancelled
    ///        run still closes and keeps the files it had written.
    bool cancelled{false};

    double elapsed_ms{};

    /// @brief Every file written: the result CSV, any income-stratified CSVs, the result metadata
    ///        JSON, and the manifest.
    std::vector<std::filesystem::path> outputs;

    std::filesystem::path result_csv;
    std::filesystem::path result_json;
    std::filesystem::path manifest;

    /// @brief How many scenario-years were simulated. Short of the horizon if cancelled.
    std::size_t years_completed{};

    /// @brief The master seed used.
    std::uint32_t seed{};
};

/// @brief Loads and validates a configuration file.
///
/// Reports every problem it finds rather than stopping at the first, and returns nullopt if any of
/// them is an error. Warnings — an applied default, for instance — come back alongside a usable
/// configuration, so a caller must read `report` even on success.
std::optional<Configuration> load_configuration(const std::filesystem::path &path,
                                                const LoadOptions &options, Report &report);

/// @brief Resolves the configuration's `data.source` to a directory and opens the store.
///
/// A directory is used in place; a local `.zip` or an `https` URL is verified against
/// `data.checksum` — which is required for both — and extracted once into a content-addressed
/// cache. A wrong checksum, an unreachable URL and a missing table are all diagnostics rather than
/// exceptions.
std::optional<DataHandle> resolve_data(const Configuration &configuration, Report &report);

/// @brief Loads every input and builds every scenario's modules.
///
/// This is where model definition files, the input dataset, the disease tables, the life table and
/// the demographic projections are read and cross-checked. It is the slowest of the three
/// validating steps and the one that catches the most.
std::optional<Run> build_run(const Configuration &configuration, const DataHandle &data,
                             Report &report);

/// @brief Runs it, writes the results, and returns what happened.
///
/// @param subscriber Receives progress events, or null for none. Delivered synchronously on this
///        thread; see EventSubscriber for what that means.
/// @param cancellation Checked between years and between scenarios. A default-constructed token
///        is never cancelled.
/// @param report Accumulates anything the run itself found. Warnings raised here are also
///        delivered to the subscriber as they happen.
///
/// @throws Nothing that a caller is expected to handle. A broken invariant inside the engine comes
///         out as `std::runtime_error` carrying a source location, because it is a defect here
///         rather than a problem with the caller's input.
RunSummary execute(Run &run, const RunOptions &options, EventSubscriber *subscriber,
                   const CancellationToken &cancellation, Report &report);

/// @brief `execute` with no subscriber and no cancellation, for a caller that wants neither.
RunSummary execute(Run &run, const RunOptions &options, Report &report);

} // namespace hgps::api
