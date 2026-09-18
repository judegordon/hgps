#include "run_store.h"

#include "json_view.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#include <fmt/format.h>

namespace hgps::server {
namespace {

std::string utc_now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
    gmtime_r(&now, &parts);
    return fmt::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", parts.tm_year + 1900,
                       parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec);
}

std::string scenario_kind(api::ScenarioKind kind) { return std::string{api::to_string(kind)}; }

} // namespace

std::string_view to_string(RunState state) noexcept {
    switch (state) {
    case RunState::starting:
        return "starting";
    case RunState::running:
        return "running";
    case RunState::completed:
        return "completed";
    case RunState::cancelled:
        return "cancelled";
    case RunState::failed:
        return "failed";
    }
    return "unknown";
}

// --- RunRecord ----------------------------------------------------------------------------------

RunRecord::RunRecord(std::string id, std::string example, std::filesystem::path folder)
    : id_{std::move(id)}, example_{std::move(example)}, folder_{std::move(folder)},
      started_utc_{utc_now()} {}

RunState RunRecord::state() const {
    const std::lock_guard lock{mutex_};
    return state_;
}

void RunRecord::set_state(RunState state) {
    {
        const std::lock_guard lock{mutex_};
        state_ = state;
        if (state == RunState::completed || state == RunState::cancelled ||
            state == RunState::failed) {
            finished_utc_ = utc_now();
        }
    }
    // A `state` message of its own, so a client watching the stream learns the run ended even if
    // it missed run_completed — and so a Cancel button has something to change state on
    // (docs/api.md: cancellation is observed at the end of a year, not at the call).
    append("state", {{"state", std::string{to_string(state)}}});
}

void RunRecord::append(std::string type, nlohmann::json payload) {
    {
        const std::lock_guard lock{mutex_};
        payload["type"] = type;
        events_.push_back(BufferedEvent{next_sequence_++, std::move(type), std::move(payload)});
        if (events_.size() > kEventLimit) {
            const auto excess = static_cast<std::ptrdiff_t>(events_.size() - kEventLimit);
            events_.erase(events_.begin(), events_.begin() + excess);
            truncated_ = true;
        }
    }
    changed_.notify_all();
}

std::vector<BufferedEvent> RunRecord::events_since(std::size_t after,
                                                   std::chrono::milliseconds wait) {
    std::unique_lock lock{mutex_};
    const auto has_more = [&] {
        return (!events_.empty() && events_.back().sequence > after) || state_ == RunState::completed ||
               state_ == RunState::cancelled || state_ == RunState::failed;
    };
    changed_.wait_for(lock, wait, has_more);

    std::vector<BufferedEvent> out;
    for (const auto &event : events_) {
        if (event.sequence > after) {
            out.push_back(event);
        }
    }
    return out;
}

std::vector<BufferedEvent> RunRecord::all_events() const {
    const std::lock_guard lock{mutex_};
    return events_;
}

bool RunRecord::finished() const {
    const std::lock_guard lock{mutex_};
    return state_ == RunState::completed || state_ == RunState::cancelled ||
           state_ == RunState::failed;
}

bool RunRecord::truncated() const {
    const std::lock_guard lock{mutex_};
    return truncated_;
}

void RunRecord::cancel() {
    cancellation_requested_.store(true, std::memory_order_relaxed);
    cancellation_.cancel();
    append("cancel_requested", {{"note", "the run stops at the end of the year it is in"}});
}

bool RunRecord::cancellation_requested() const noexcept {
    return cancellation_requested_.load(std::memory_order_relaxed);
}

void RunRecord::set_description(nlohmann::json description) {
    const std::lock_guard lock{mutex_};
    description_ = std::move(description);
}

void RunRecord::set_diagnostics(nlohmann::json diagnostics) {
    const std::lock_guard lock{mutex_};
    diagnostics_ = std::move(diagnostics);
}

void RunRecord::set_failure(std::string message) {
    const std::lock_guard lock{mutex_};
    failure_ = std::move(message);
}

void RunRecord::note_year_completed() {
    const std::lock_guard lock{mutex_};
    ++years_completed_;
}

void RunRecord::set_total_years(std::size_t total) {
    const std::lock_guard lock{mutex_};
    total_years_ = total;
}

void RunRecord::set_elapsed_ms(double elapsed) {
    const std::lock_guard lock{mutex_};
    elapsed_ms_ = elapsed;
}

std::optional<nlohmann::json> RunRecord::manifest() const {
    // Read from disk rather than remembered: the manifest on disk is the record, and reading it
    // means a served manifest and a downloaded one cannot disagree.
    const auto found = find_manifest(folder_);
    if (!found.has_value()) {
        return std::nullopt;
    }
    std::ifstream stream{*found};
    if (!stream) {
        return std::nullopt;
    }
    try {
        nlohmann::json document;
        stream >> document;
        return document;
    } catch (const nlohmann::json::exception &) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> find_manifest(const std::filesystem::path &folder) {
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator{folder, error}) {
        if (!entry.is_regular_file(error)) {
            continue;
        }
        const auto name = entry.path().filename().string();
        if (name.ends_with("_manifest.json") || name == "manifest.json") {
            return entry.path();
        }
    }
    return std::nullopt;
}

std::filesystem::path RunRecord::result_csv() const {
    // The main CSV, not one of the income-stratified ones. Their names are the main one's stem
    // plus a suffix, so the shortest `.csv` name is the main one — which is stabler than matching
    // "LowIncome", "MiddleIncome" and "HighIncome" by spelling.
    std::filesystem::path best;
    for (const auto &name : results()) {
        if (std::filesystem::path{name}.extension() != ".csv") {
            continue;
        }
        if (best.empty() || name.size() < best.filename().string().size()) {
            best = folder_ / name;
        }
    }
    return best;
}

std::vector<std::string> RunRecord::results() const {
    std::vector<std::string> names;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator{folder_, error}) {
        if (entry.is_regular_file(error)) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

nlohmann::json RunRecord::describe(bool include_manifest) const {
    nlohmann::json out;
    {
        const std::lock_guard lock{mutex_};
        out = {
            {"id", id_},
            {"example", example_},
            {"state", std::string{to_string(state_)}},
            {"started_utc", started_utc_},
            {"finished_utc", finished_utc_.empty() ? nlohmann::json(nullptr)
                                                   : nlohmann::json(finished_utc_)},
            {"years_completed", years_completed_},
            {"total_years", total_years_},
            {"elapsed_ms", elapsed_ms_},
            {"cancelled", state_ == RunState::cancelled},
            {"output_folder", folder_.string()},
            {"diagnostics", diagnostics_},
        };
        out["description"] = description_.is_null() ? nlohmann::json(nullptr) : description_;
        out["error"] = failure_.empty() ? nlohmann::json(nullptr) : nlohmann::json(failure_);
    }

    if (include_manifest) {
        const auto found = manifest();
        out["manifest"] = found.has_value() ? *found : nlohmann::json(nullptr);
        out["results"] = results();
    }
    return out;
}

// --- RunStore -----------------------------------------------------------------------------------

RunStore::RunStore(std::filesystem::path root) : root_{std::move(root)} {
    std::error_code error;
    std::filesystem::create_directories(root_, error);
}

RunStore::~RunStore() { join_active(); }

std::string RunStore::next_id() const {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm parts{};
    gmtime_r(&seconds, &parts);

    // A counter rather than a random suffix: two runs started in the same second must not collide,
    // and an id that is reproducible from the sequence is easier to reason about in a test than
    // one that is not. The counter is process-wide and monotonic.
    static std::atomic<unsigned int> counter{0};
    const auto ordinal = counter.fetch_add(1, std::memory_order_relaxed);

    return fmt::format("{:04}{:02}{:02}T{:02}{:02}{:02}Z-{:04x}", parts.tm_year + 1900,
                       parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec,
                       ordinal & 0xffffU);
}

std::shared_ptr<RunRecord> RunStore::begin(const std::string &id, const std::string &example) {
    const std::lock_guard lock{mutex_};
    if (active_ != nullptr) {
        return nullptr;
    }
    auto record = std::make_shared<RunRecord>(id, example, root_ / id);
    std::error_code error;
    std::filesystem::create_directories(record->folder(), error);

    records_.push_back(record);
    active_ = record;
    return record;
}

void RunStore::finish(const std::string &id) {
    // Called from the run's own thread, as its last act. It clears the slot and touches `worker_`
    // not at all: a thread cannot join itself, and detaching it here would make `join_active`
    // return while the run was still unwinding.
    const std::lock_guard lock{mutex_};
    if (active_ != nullptr && active_->id() == id) {
        active_.reset();
    }
}

void RunStore::discard(const std::string &id) {
    std::filesystem::path folder;
    {
        const std::lock_guard lock{mutex_};
        if (active_ != nullptr && active_->id() == id) {
            active_.reset();
        }
        for (auto it = records_.begin(); it != records_.end(); ++it) {
            if ((*it)->id() == id) {
                folder = (*it)->folder();
                records_.erase(it);
                break;
            }
        }
    }
    if (!folder.empty()) {
        // Only if it is empty: a directory with anything in it is not this function's to remove.
        std::error_code error;
        if (std::filesystem::is_empty(folder, error) && !error) {
            std::filesystem::remove(folder, error);
        }
    }
}

std::shared_ptr<RunRecord> RunStore::find(const std::string &id) const {
    const std::lock_guard lock{mutex_};
    for (const auto &record : records_) {
        if (record->id() == id) {
            return record;
        }
    }
    return nullptr;
}

std::shared_ptr<RunRecord> RunStore::active() const {
    const std::lock_guard lock{mutex_};
    return active_;
}

void RunStore::adopt_thread(std::thread thread) {
    std::thread previous;
    {
        const std::lock_guard lock{mutex_};
        previous = std::move(worker_);
        worker_ = std::move(thread);
    }
    // Outside the lock, and joined rather than detached: the previous run released the slot as its
    // last act — which is what let this one start — so it is on the point of returning.
    if (previous.joinable()) {
        previous.join();
    }
}

void RunStore::join_active() {
    std::thread worker;
    {
        const std::lock_guard lock{mutex_};
        worker = std::move(worker_);
    }
    if (worker.joinable()) {
        worker.join();
    }
}

nlohmann::json RunStore::list() const {
    std::vector<std::shared_ptr<RunRecord>> in_process;
    std::string active_id;
    {
        const std::lock_guard lock{mutex_};
        in_process = records_;
        if (active_ != nullptr) {
            active_id = active_->id();
        }
    }

    auto runs = nlohmann::json::array();
    std::vector<std::string> seen;
    for (const auto &record : in_process) {
        runs.push_back(record->describe(false));
        seen.push_back(record->id());
    }

    // Everything else in the runs directory, read from its manifest. This is what makes the
    // history survive a restart and makes a copied runs directory list correctly.
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator{root_, error}) {
        if (!entry.is_directory(error)) {
            continue;
        }
        const auto id = entry.path().filename().string();
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
            continue;
        }
        const auto manifest_path = find_manifest(entry.path());
        if (!manifest_path.has_value()) {
            continue;
        }
        std::ifstream stream{*manifest_path};
        if (!stream) {
            continue;
        }
        nlohmann::json manifest;
        try {
            stream >> manifest;
        } catch (const nlohmann::json::exception &) {
            continue;
        }
        const auto run = manifest.value("run", nlohmann::json::object());
        runs.push_back({
            {"id", id},
            {"example", std::filesystem::path{manifest.value("config", nlohmann::json::object())
                                                  .value("path", std::string{})}
                            .parent_path()
                            .filename()
                            .string()},
            {"state", run.value("cancelled", false) ? "cancelled" : "completed"},
            {"started_utc", manifest.value("timing", nlohmann::json::object())
                                .value("started_utc", std::string{})},
            {"finished_utc", manifest.value("timing", nlohmann::json::object())
                                 .value("finished_utc", std::string{})},
            {"years_completed", run.value("years_completed", 0)},
            {"total_years", 0},
            {"elapsed_ms", manifest.value("timing", nlohmann::json::object())
                               .value("elapsed_ms", 0.0)},
            {"cancelled", run.value("cancelled", false)},
            {"output_folder", entry.path().string()},
            {"diagnostics", nlohmann::json::array()},
            {"description", nullptr},
            {"error", nullptr},
        });
    }

    // Newest first, by id, which is time-ordered by construction.
    std::sort(runs.begin(), runs.end(), [](const nlohmann::json &left, const nlohmann::json &right) {
        return left.value("id", std::string{}) > right.value("id", std::string{});
    });

    return {{"active", active_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(active_id)},
            {"runs", runs}};
}

// --- RecordingSubscriber -------------------------------------------------------------------------

RecordingSubscriber::RecordingSubscriber(std::shared_ptr<RunRecord> record)
    : record_{std::move(record)} {}

void RecordingSubscriber::on_run_started(const api::RunStarted &event) {
    record_->set_total_years(event.total_years);
    record_->set_state(RunState::running);
    record_->append("run_started", {{"engine_version", event.engine_version},
                                    {"seed", event.seed},
                                    {"trial_runs", event.trial_runs},
                                    {"start_time", event.start_time},
                                    {"stop_time", event.stop_time},
                                    {"cohort_size", event.cohort_size},
                                    {"scenarios", event.scenarios},
                                    {"total_years", event.total_years}});
}

void RecordingSubscriber::on_scenario_started(const api::ScenarioStarted &event) {
    record_->append("scenario_started", {{"scenario", event.scenario},
                                         {"kind", scenario_kind(event.kind)},
                                         {"run", event.run}});
}

void RecordingSubscriber::on_year_completed(const api::YearCompleted &event) {
    record_->note_year_completed();
    record_->append("year_completed", {{"scenario", event.scenario},
                                       {"kind", scenario_kind(event.kind)},
                                       {"run", event.run},
                                       {"year", event.year},
                                       {"elapsed_ms", event.elapsed_ms},
                                       {"population", event.population_size}});
}

void RecordingSubscriber::on_scenario_completed(const api::ScenarioCompleted &event) {
    record_->append("scenario_completed", {{"scenario", event.scenario},
                                           {"kind", scenario_kind(event.kind)},
                                           {"run", event.run},
                                           {"elapsed_ms", event.elapsed_ms},
                                           {"years_completed", event.years_completed}});
}

void RecordingSubscriber::on_run_completed(const api::RunCompleted &event) {
    record_->set_elapsed_ms(event.elapsed_ms);
    auto files = nlohmann::json::array();
    for (const auto &path : event.outputs) {
        files.push_back(path.filename().string());
    }
    record_->append("run_completed", {{"elapsed_ms", event.elapsed_ms},
                                      {"cancelled", event.cancelled},
                                      {"outputs", files}});
}

void RecordingSubscriber::on_diagnostic(const api::Diagnostic &diagnostic) {
    record_->append("diagnostic", to_json(diagnostic));
}

} // namespace hgps::server
