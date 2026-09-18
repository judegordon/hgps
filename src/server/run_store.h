// The runs a server knows about: the one that is going, and every one whose manifest is on disk.
//
// There is no database. A completed run is its manifest
// (docs/decisions/0034-a-run-manifest-beside-the-results.md), which is why a restart loses nothing
// and a runs directory copied from another machine lists correctly
// (docs/decisions/0042-a-local-server-in-the-same-binary.md).
#pragma once

#include "hgps/engine.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace hgps::server {

/// @brief How far a run has got. `starting` is the window between accepting the request and the
///        engine's first event; it exists so a client never sees a run with no state.
enum class RunState { starting, running, completed, cancelled, failed };

std::string_view to_string(RunState state) noexcept;

/// @brief One event, already formatted for the wire.
///
/// Formatted at the point of delivery rather than stored as a variant, because the engine delivers
/// events synchronously on the simulation's own thread and a slow subscriber is a slow simulation
/// (docs/api.md). Formatting is cheap; holding the lock while a client's socket drains is not, and
/// this shape makes that impossible.
struct BufferedEvent {
    std::size_t sequence{};
    std::string type;
    nlohmann::json payload;
};

/// @brief One run: its identity, its state, its events and its cancellation.
///
/// Shared between the thread running the simulation and every thread serving an HTTP request about
/// it, so everything mutable is behind `mutex`.
class RunRecord {
  public:
    RunRecord(std::string id, std::string example, std::filesystem::path folder);

    const std::string &id() const noexcept { return id_; }
    const std::string &example() const noexcept { return example_; }
    const std::filesystem::path &folder() const noexcept { return folder_; }

    RunState state() const;
    void set_state(RunState state);

    /// @brief Appends an event and wakes every waiting reader.
    void append(std::string type, nlohmann::json payload);

    /// @brief Events from `after` onwards, and whether the run has finished.
    ///
    /// Blocks until there is something to return or the run ends, so a streaming client is not a
    /// polling client. Returns an empty vector on timeout, which is how a heartbeat gets sent.
    std::vector<BufferedEvent> events_since(std::size_t after, std::chrono::milliseconds wait);

    /// @brief Every event so far, for a client that connects late. The buffer is the replay.
    std::vector<BufferedEvent> all_events() const;

    bool finished() const;

    /// @brief True if the event buffer dropped its oldest entries. Reported to a client, because a
    ///        replay with a hole in it is not the same as a complete one.
    bool truncated() const;

    api::CancellationToken &cancellation() noexcept { return cancellation_; }
    void cancel();
    bool cancellation_requested() const noexcept;

    /// @brief What a run list and `GET /api/runs/{id}` report.
    nlohmann::json describe(bool include_manifest) const;

    void set_description(nlohmann::json description);
    void set_diagnostics(nlohmann::json diagnostics);
    void set_failure(std::string message);
    void note_year_completed();
    void set_total_years(std::size_t total);
    void set_elapsed_ms(double elapsed);

    /// @brief The manifest this run wrote, read from disk. Absent until the run has written one.
    std::optional<nlohmann::json> manifest() const;

    /// @brief The result files beside the manifest, by name.
    std::vector<std::string> results() const;

  private:
    /// @brief How many events one run may keep. A full-scale HLM_India emits about 80
    ///        `year_completed` events, so this is generous; a run that exceeded it would be
    ///        something new, and dropping the oldest while saying so beats growing without bound.
    static constexpr std::size_t kEventLimit = 100000;

    std::string id_;
    std::string example_;
    std::filesystem::path folder_;

    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;

    RunState state_{RunState::starting};
    std::vector<BufferedEvent> events_;
    std::size_t next_sequence_{1};
    bool truncated_{false};

    nlohmann::json description_;
    nlohmann::json diagnostics_ = nlohmann::json::array();
    std::string failure_;
    std::size_t years_completed_{};
    std::size_t total_years_{};
    double elapsed_ms_{};
    std::string started_utc_;
    std::string finished_utc_;

    api::CancellationToken cancellation_;
    std::atomic<bool> cancellation_requested_{false};
};

/// @brief Every run: the active one, and the completed ones on disk.
class RunStore {
  public:
    explicit RunStore(std::filesystem::path root);

    const std::filesystem::path &root() const noexcept { return root_; }

    /// @brief A new run id: time-ordered, so a listing sorts without parsing a date, and unique
    ///        within a second, so two runs a second apart cannot collide.
    std::string next_id() const;

    /// @brief Registers a run as the active one.
    /// @returns nullptr if another run is already active — refused, not queued
    ///          (docs/server-api.md, "One run at a time").
    std::shared_ptr<RunRecord> begin(const std::string &id, const std::string &example);

    /// @brief Releases the active slot. The record stays reachable by id.
    void finish(const std::string &id);

    /// @brief Releases the slot and forgets the run entirely, removing its empty directory.
    ///
    /// For a run that was accepted and then failed to *build* — a bad configuration, a data pack
    /// that will not resolve. Nothing was simulated and nothing was written, so it is not a run:
    /// leaving it in the list as "starting" for ever is what happened before this existed, and
    /// recording it as a failed run would put a row in the history that vanishes on restart,
    /// because the history is read from manifests and there is no manifest.
    void discard(const std::string &id);

    std::shared_ptr<RunRecord> find(const std::string &id) const;
    std::shared_ptr<RunRecord> active() const;

    /// @brief Every run: the in-process ones, plus every manifest in the runs directory that no
    ///        in-process record already covers. Newest first.
    nlohmann::json list() const;

    /// @brief Waits for the active run's thread, if any. For shutdown and for tests.
    void join_active();

    void adopt_thread(std::thread thread);

  private:
    std::filesystem::path root_;

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<RunRecord>> records_;
    std::shared_ptr<RunRecord> active_;
    std::thread worker_;
};

/// @brief The `EventSubscriber` a served run uses: format, append, return.
///
/// It does the least it can, because the engine delivers events synchronously on the simulation's
/// own thread (docs/api.md). Every HTTP connection is served from the record's buffer, never from
/// this callback.
class RecordingSubscriber final : public api::EventSubscriber {
  public:
    explicit RecordingSubscriber(std::shared_ptr<RunRecord> record);

    void on_run_started(const api::RunStarted &event) override;
    void on_scenario_started(const api::ScenarioStarted &event) override;
    void on_year_completed(const api::YearCompleted &event) override;
    void on_scenario_completed(const api::ScenarioCompleted &event) override;
    void on_run_completed(const api::RunCompleted &event) override;
    void on_diagnostic(const api::Diagnostic &diagnostic) override;

  private:
    std::shared_ptr<RunRecord> record_;
};

} // namespace hgps::server
