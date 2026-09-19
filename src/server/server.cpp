#include "server.h"

#include "json_view.h"
#include "run_store.h"
#include "summary.h"

#include "hgps/engine.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace hgps::server {
namespace {

/// @brief This document's version. Changes when a client would have to.
constexpr int kApiVersion = 1;

void no_store(httplib::Response &response) {
    // The server is a local tool over a directory that changes underneath it, so a stale run list
    // is a bug a cache would cause.
    response.set_header("Cache-Control", "no-store");
}

void send_json(httplib::Response &response, const nlohmann::json &document, int status = 200) {
    no_store(response);
    response.status = status;
    response.set_content(document.dump(2) + "\n", "application/json; charset=utf-8");
}

void send_error(httplib::Response &response, int status, const std::string &code,
                const std::string &message, const api::Report *report = nullptr) {
    send_json(response, error_document(code, message, report), status);
}

/// @brief Resolves a path and refuses it if it leaves every allowed root.
///
/// Symlinks are followed first, so a link out of a root is caught rather than followed — the same
/// rule the equivalence harness's scratch directories follow (ADR 0039).
std::optional<std::filesystem::path> within_roots(
    const std::filesystem::path &candidate, const std::vector<std::filesystem::path> &roots) {
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        return std::nullopt;
    }
    for (const auto &root : roots) {
        const auto base = std::filesystem::weakly_canonical(root, error);
        if (error) {
            continue;
        }
        auto mismatch = std::mismatch(base.begin(), base.end(), resolved.begin(), resolved.end());
        if (mismatch.first == base.end()) {
            return resolved;
        }
    }
    return std::nullopt;
}

/// @brief An example id is a single directory name. Not a path, so there is nothing to escape.
bool is_plain_name(const std::string &name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

std::string media_type_of(const std::filesystem::path &path) {
    const auto extension = path.extension().string();
    if (extension == ".csv") {
        return "text/csv; charset=utf-8";
    }
    if (extension == ".json") {
        return "application/json; charset=utf-8";
    }
    if (extension == ".html") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".js" || extension == ".mjs") {
        return "text/javascript; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    return "application/octet-stream";
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// @brief One server-sent event.
std::string sse_frame(const BufferedEvent &event) {
    return fmt::format("id: {}\nevent: {}\ndata: {}\n\n", event.sequence, event.type,
                       event.payload.dump());
}

} // namespace

std::string loopback_refusal(const std::string &host) {
    if (host == "127.0.0.1" || host == "::1" || host == "localhost") {
        return {};
    }
    return fmt::format(
        "refusing to bind to '{}': this server has no authentication, and it is only safe without "
        "one because it cannot be reached from another machine. A configuration names files to "
        "read and a folder to write, so a simulation host on a reachable address is a remote code "
        "execution surface. Use 127.0.0.1, ::1 or localhost, and put a reverse proxy in front if "
        "this should be reachable — that is where the decision about who may run simulations on "
        "this machine belongs (docs/server-api.md).",
        host);
}

// --- the implementation -------------------------------------------------------------------------

class Server::Impl {
  public:
    explicit Impl(Options options)
        : options_{std::move(options)}, runs_{options_.runs_root} {
        install_routes();
    }

    bool bind() {
        const auto refusal = loopback_refusal(options_.host);
        if (!refusal.empty()) {
            fmt::print(stderr, "hgps serve: {}\n", refusal);
            return false;
        }
        // Port 0 means "any free one", and the only way to learn which is to ask for it that way
        // — which is what a test wants, so that two tests never fight over a fixed port.
        if (options_.port == 0) {
            const auto bound = server_.bind_to_any_port(options_.host.c_str());
            if (bound < 0) {
                fmt::print(stderr, "hgps serve: could not bind {} to any port\n", options_.host);
                return false;
            }
            port_ = static_cast<std::uint16_t>(bound);
            return true;
        }
        if (!server_.bind_to_port(options_.host.c_str(), options_.port)) {
            fmt::print(stderr, "hgps serve: could not bind {}:{}\n", options_.host, options_.port);
            return false;
        }
        port_ = options_.port;
        return true;
    }

    bool serve() { return server_.listen_after_bind(); }

    /// @brief Blocks until the serving thread is actually accepting, or has given up.
    ///
    /// Needed because cpp-httplib's `stop()` does nothing at all unless the server is already
    /// running: it tests `is_running_`, which `listen_after_bind` sets from the serving thread. So
    /// a server started and then stopped — or dropped — before that thread got there kept its
    /// socket open, and the join in `Server::stop` blocked for ever. It is a race, so it happened
    /// only when the stop won, which is why `AStartedServerCanSimplyBeDropped` never saw it: that
    /// test makes a request first, and a request that gets an answer proves the loop is running.
    void wait_until_ready() { server_.wait_until_ready(); }

    void stop() {
        server_.stop();

        // And the run, if one is going. Without this, stopping the server detaches a thread that
        // is still writing a result file and then returns from main — so Ctrl-C during a run
        // could truncate its output, which is the one thing this project's output contract
        // cannot tolerate.
        //
        // Cancelling rather than waiting for the horizon: the engine stops at the end of the year
        // it is in and closes its files, so a cancelled run is a *prefix* of the run that would
        // have happened (docs/api.md). That is exactly what Ctrl-C should mean.
        if (const auto active = runs_.active(); active != nullptr) {
            active->cancel();
        }
        runs_.join_active();
    }

    std::uint16_t port() const noexcept { return port_; }

  private:
    // --- routes ---------------------------------------------------------------------------------

    void install_routes() {
        server_.Get("/api/version", [](const httplib::Request &, httplib::Response &response) {
            send_json(response, version_document(kApiVersion));
        });

        server_.Get("/api/examples", [this](const httplib::Request &, httplib::Response &r) {
            list_examples(r);
        });
        server_.Get(R"(/api/examples/([^/]+))",
                    [this](const httplib::Request &q, httplib::Response &r) { get_example(q, r); });

        server_.Post("/api/configs/validate",
                     [this](const httplib::Request &q, httplib::Response &r) { validate(q, r); });

        server_.Get("/api/schema", [this](const httplib::Request &, httplib::Response &r) {
            get_schema(r);
        });

        server_.Get("/api/runs", [this](const httplib::Request &, httplib::Response &r) {
            send_json(r, runs_.list());
        });
        server_.Post("/api/runs",
                     [this](const httplib::Request &q, httplib::Response &r) { start_run(q, r); });
        server_.Get(R"(/api/runs/([^/]+))",
                    [this](const httplib::Request &q, httplib::Response &r) { get_run(q, r); });
        server_.Post(R"(/api/runs/([^/]+)/cancel)",
                     [this](const httplib::Request &q, httplib::Response &r) { cancel_run(q, r); });
        server_.Get(R"(/api/runs/([^/]+)/events)",
                    [this](const httplib::Request &q, httplib::Response &r) { stream(q, r); });
        server_.Get(R"(/api/runs/([^/]+)/summary)",
                    [this](const httplib::Request &q, httplib::Response &r) { summary(q, r); });
        server_.Get(R"(/api/runs/([^/]+)/results/([^/]+))",
                    [this](const httplib::Request &q, httplib::Response &r) { result(q, r); });

        // Anything else under /api is a mistake worth naming, rather than the frontend's index
        // page served with a 200 — which is what a catch-all would do and is the single most
        // confusing thing a JSON client can be given.
        server_.Get(R"(/api/.*)", [](const httplib::Request &q, httplib::Response &r) {
            send_error(r, 404, "not_found", fmt::format("no such endpoint: {}", q.path));
        });

        if (!options_.web_root.empty()) {
            install_static_routes();
        }

        server_.set_exception_handler(
            [](const httplib::Request &, httplib::Response &response, std::exception_ptr pointer) {
                std::string message = "an unexpected failure";
                try {
                    std::rethrow_exception(pointer);
                } catch (const std::exception &error) {
                    message = error.what();
                } catch (...) {
                }
                send_error(response, 500, "internal_error", message);
            });
    }

    void install_static_routes() {
        server_.set_mount_point("/", options_.web_root.string());

        // A single-page app owns its own routing, so a path with no file behind it is the index
        // page rather than a 404 — but only for a navigation, never for /api and never for
        // something that looks like a missing asset, which should fail loudly.
        server_.set_file_request_handler([](const httplib::Request &, httplib::Response &response) {
            no_store(response);
        });
        const auto index = options_.web_root / "index.html";
        server_.set_error_handler([index](const httplib::Request &request,
                                          httplib::Response &response) {
            if (response.status != 404 || request.path.starts_with("/api") ||
                std::filesystem::path{request.path}.has_extension()) {
                return;
            }
            std::error_code error;
            if (!std::filesystem::is_regular_file(index, error)) {
                return;
            }
            response.status = 200;
            response.set_content(read_file(index), "text/html; charset=utf-8");
        });
    }

    // --- examples --------------------------------------------------------------------------------

    void list_examples(httplib::Response &response) {
        auto examples = nlohmann::json::array();
        for (const auto &root : options_.config_roots) {
            std::error_code error;
            if (!std::filesystem::is_directory(root, error)) {
                continue;
            }
            for (const auto &entry : std::filesystem::directory_iterator{root, error}) {
                if (!entry.is_directory(error)) {
                    continue;
                }
                const auto config = entry.path() / "config.json";
                if (!std::filesystem::is_regular_file(config, error)) {
                    continue;
                }
                // Listed, not loaded: a directory of a hundred configs must not cost a hundred
                // data-pack resolutions, and a config that will not load is still worth showing.
                examples.push_back({
                    {"id", entry.path().filename().string()},
                    {"path", std::filesystem::relative(config, root, error).string()},
                    {"root", root.string()},
                    {"readable", std::ifstream{config}.good()},
                });
            }
        }
        std::sort(examples.begin(), examples.end(),
                  [](const nlohmann::json &a, const nlohmann::json &b) {
                      return a.value("id", std::string{}) < b.value("id", std::string{});
                  });
        send_json(response, {{"examples", examples}});
    }

    std::optional<std::filesystem::path> config_for(const std::string &id) const {
        if (!is_plain_name(id)) {
            return std::nullopt;
        }
        for (const auto &root : options_.config_roots) {
            const auto candidate = root / id / "config.json";
            std::error_code error;
            if (!std::filesystem::is_regular_file(candidate, error)) {
                continue;
            }
            if (const auto safe = within_roots(candidate, options_.config_roots)) {
                return safe;
            }
        }
        return std::nullopt;
    }

    void get_example(const httplib::Request &request, httplib::Response &response) {
        const auto id = request.matches[1].str();
        const auto path = config_for(id);
        if (!path.has_value()) {
            send_error(response, 404, "not_found", fmt::format("no example called '{}'", id));
            return;
        }

        api::Report report;
        auto configuration = api::load_configuration(*path, {}, report);

        nlohmann::json document;
        try {
            std::ifstream stream{*path};
            stream >> document;
        } catch (const nlohmann::json::exception &error) {
            send_error(response, 422, "config_invalid",
                       fmt::format("{} is not valid JSON: {}", path->string(), error.what()));
            return;
        }

        if (!configuration.has_value()) {
            send_json(response,
                      error_document("config_invalid",
                                     fmt::format("{} has {} error(s)", id, report.error_count()),
                                     &report),
                      422);
            return;
        }

        send_json(response, {{"id", id},
                             {"path", path->string()},
                             {"sha256", configuration->sha256()},
                             {"document", document},
                             {"summary", summary_of(*configuration)},
                             {"diagnostics", to_json(report)}});
    }

    // --- validation ------------------------------------------------------------------------------

    void validate(const httplib::Request &request, httplib::Response &response) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(request.body);
        } catch (const nlohmann::json::exception &error) {
            send_error(response, 400, "bad_request",
                       fmt::format("the request body is not valid JSON: {}", error.what()));
            return;
        }
        if (!body.is_object() || !body.contains("document")) {
            send_error(response, 400, "bad_request", "the request needs a 'document' member");
            return;
        }

        // The loader reads a file, so the document is written to one — in the directory whose
        // relative paths it is meant to resolve against, so `base` means what it says.
        std::filesystem::path directory;
        if (const auto base = body.value("base", std::string{}); !base.empty()) {
            const auto config = config_for(base);
            if (!config.has_value()) {
                send_error(response, 404, "not_found",
                           fmt::format("no example called '{}' to resolve paths against", base));
                return;
            }
            directory = config->parent_path();
        } else {
            directory = options_.config_roots.empty() ? std::filesystem::current_path()
                                                      : options_.config_roots.front();
        }

        // The loader reads a *file*, and a model file names its own CSVs by a path relative to
        // the config's directory — so a document being validated has to be written where its
        // relative paths mean what they mean. Hence a scratch file in the example's own directory
        // rather than a temporary one somewhere neutral.
        //
        // The name carries a counter as well as the body's hash: httplib serves requests on
        // several threads, and two clients validating the *same* document would otherwise pick
        // the same name — whereupon the first to finish would delete the file the second was
        // still loading, and the second would be told its own document does not exist.
        static std::atomic<std::uint64_t> validations{0};
        const auto scratch =
            directory / fmt::format(".hgps-validate-{:016x}-{}.json",
                                    std::hash<std::string>{}(request.body),
                                    validations.fetch_add(1, std::memory_order_relaxed));
        {
            std::ofstream stream{scratch, std::ios::trunc};
            if (!stream) {
                send_error(response, 500, "internal_error",
                           fmt::format("could not write a scratch config into {}",
                                       directory.string()));
                return;
            }
            stream << body["document"].dump(2) << '\n';
        }
        struct Remove {
            std::filesystem::path path;
            ~Remove() {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } remove{scratch};

        api::LoadOptions options;
        options.require_files_exist = body.value("require_files_exist", true);
        for (const auto &name : body.value("baseline_compat", std::vector<std::string>{})) {
            if (!api::BaselineCompat::apply_name(name, options.baseline_compat)) {
                send_error(
                    response, 400, "bad_request",
                    fmt::format("'{}' is not a baseline compatibility flag; the flags are {}", name,
                                api::BaselineCompat::known_names_sentence()));
                return;
            }
        }

        api::Report report;
        const auto configuration = api::load_configuration(scratch, options, report);

        nlohmann::json out{{"valid", configuration.has_value()},
                           {"error_count", report.error_count()},
                           {"warning_count", report.warning_count()},
                           {"diagnostics", to_json(report)}};
        out["summary"] = configuration.has_value() ? summary_of(*configuration)
                                                   : nlohmann::json(nullptr);
        send_json(response, out);
    }

    void get_schema(httplib::Response &response) {
        std::error_code error;
        if (options_.schema_path.empty() ||
            !std::filesystem::is_regular_file(options_.schema_path, error)) {
            send_error(response, 404, "not_found",
                       "this server was started without a config schema to publish");
            return;
        }
        try {
            send_json(response, {{"schema", inline_schema_refs(options_.schema_path)},
                                 {"version", 2},
                                 {"source", options_.schema_path.filename().string()}});
        } catch (const std::exception &failure) {
            send_error(response, 500, "internal_error", failure.what());
        }
    }

    // --- runs -----------------------------------------------------------------------------------

    void start_run(const httplib::Request &request, httplib::Response &response) {
        nlohmann::json body;
        try {
            body = request.body.empty() ? nlohmann::json::object()
                                        : nlohmann::json::parse(request.body);
        } catch (const nlohmann::json::exception &error) {
            send_error(response, 400, "bad_request",
                       fmt::format("the request body is not valid JSON: {}", error.what()));
            return;
        }

        const auto example = body.value("example", std::string{});
        if (example.empty()) {
            send_error(response, 400, "bad_request", "the request needs an 'example'");
            return;
        }
        const auto config = config_for(example);
        if (!config.has_value()) {
            send_error(response, 404, "not_found", fmt::format("no example called '{}'", example));
            return;
        }

        if (const auto active = runs_.active(); active != nullptr) {
            send_json(response,
                      error_document("run_in_progress",
                                     fmt::format("run {} is still going; this engine runs one "
                                                 "simulation at a time (docs/api.md)",
                                                 active->id())),
                      409);
            return;
        }

        const auto id = runs_.next_id();
        auto record = runs_.begin(id, example);
        if (record == nullptr) {
            send_error(response, 409, "run_in_progress", "another run started first");
            return;
        }

        api::LoadOptions load_options;
        load_options.output_folder_override = record->folder().string();
        for (const auto &name : body.value("baseline_compat", std::vector<std::string>{})) {
            if (!api::BaselineCompat::apply_name(name, load_options.baseline_compat)) {
                runs_.discard(id);
                send_error(
                    response, 400, "bad_request",
                    fmt::format("'{}' is not a baseline compatibility flag; the flags are {}", name,
                                api::BaselineCompat::known_names_sentence()));
                return;
            }
        }

        api::RunOptions run_options;
        run_options.threads = body.value("threads", std::size_t{1});
        run_options.write_manifest = body.value("write_manifest", true);

        // Loaded, resolved and *built* before the response is sent: building is where bad input
        // fails, so a 201 means the run will almost certainly finish and a configuration problem
        // comes back as a 422 rather than as a run that dies a second later (docs/server-api.md).
        api::Report report;
        auto configuration = api::load_configuration(*config, load_options, report);
        if (!configuration.has_value()) {
            runs_.discard(id);
            send_json(response,
                      error_document("config_invalid",
                                     fmt::format("{} has {} error(s)", example,
                                                 report.error_count()),
                                     &report),
                      422);
            return;
        }
        auto data = api::resolve_data(*configuration, report);
        if (!data.has_value()) {
            runs_.discard(id);
            send_json(response,
                      error_document("config_invalid",
                                     fmt::format("{}'s data could not be resolved", example),
                                     &report),
                      422);
            return;
        }
        auto run = api::build_run(*configuration, *data, report);
        if (!run.has_value()) {
            runs_.discard(id);
            send_json(response,
                      error_document("config_invalid",
                                     fmt::format("{} could not be built", example), &report),
                      422);
            return;
        }

        record->set_description(to_json(run->description()));
        record->set_diagnostics(to_json(report));
        record->set_total_years(run->description().scenarios.size() *
                                run->description().trial_runs *
                                static_cast<std::size_t>(run->description().stop_time -
                                                         run->description().start_time + 1));

        auto worker = std::thread([this, record, id, run = std::move(run),
                                   configuration = std::move(configuration),
                                   data = std::move(data), run_options]() mutable {
            RecordingSubscriber subscriber{record};
            api::Report run_report;
            try {
                const auto summary = api::execute(*run, run_options, &subscriber,
                                                  record->cancellation(), run_report);
                record->set_diagnostics(to_json(run_report));
                record->set_state(summary.cancelled   ? RunState::cancelled
                                  : summary.succeeded ? RunState::completed
                                                      : RunState::failed);
            } catch (const std::exception &failure) {
                record->set_failure(failure.what());
                record->set_state(RunState::failed);
            }
            runs_.finish(id);
        });
        runs_.adopt_thread(std::move(worker));

        auto created = record->describe(false);
        created["state"] = "starting";
        send_json(response, created, 201);
    }

    void get_run(const httplib::Request &request, httplib::Response &response) {
        const auto record = runs_.find(request.matches[1].str());
        if (record == nullptr) {
            send_error(response, 404, "not_found",
                       fmt::format("no run called '{}'", request.matches[1].str()));
            return;
        }
        send_json(response, record->describe(true));
    }

    void cancel_run(const httplib::Request &request, httplib::Response &response) {
        const auto record = runs_.find(request.matches[1].str());
        if (record == nullptr) {
            send_error(response, 404, "not_found",
                       fmt::format("no run called '{}'", request.matches[1].str()));
            return;
        }
        if (record->finished()) {
            send_error(response, 409, "run_not_active",
                       fmt::format("run {} has already finished", record->id()));
            return;
        }
        record->cancel();
        // 202, not 200: the engine observes cancellation at the end of the year it is in, so the
        // state change arrives on the event stream rather than in this response (docs/api.md).
        send_json(response,
                  {{"id", record->id()},
                   {"state", std::string{to_string(record->state())}},
                   {"note", "the run stops at the end of the year it is in; watch the event "
                            "stream for the state change"}},
                  202);
    }

    void stream(const httplib::Request &request, httplib::Response &response) {
        const auto record = runs_.find(request.matches[1].str());
        if (record == nullptr) {
            send_error(response, 404, "not_found",
                       fmt::format("no run called '{}'", request.matches[1].str()));
            return;
        }

        no_store(response);
        response.set_header("X-Accel-Buffering", "no");

        // One cursor per connection, owned by the provider. A client that connects late starts at
        // zero and is therefore sent the whole buffer before the live events — the replay
        // docs/server-api.md promises, and the reason a client's reconnect logic has one path.
        auto cursor = std::make_shared<std::size_t>(0);
        auto warned = std::make_shared<bool>(false);
        response.set_chunked_content_provider(
            "text/event-stream", [record, cursor, warned](std::size_t, httplib::DataSink &sink) {
                return pump(record, sink, *cursor, *warned);
            });
    }

    /// @brief One turn of the stream: everything buffered since the last, then wait.
    ///
    /// Served entirely from the record's buffer, never from the engine's thread, because the
    /// engine delivers events synchronously on the simulation's own thread and a slow subscriber
    /// is a slow simulation (docs/api.md).
    static bool pump(const std::shared_ptr<RunRecord> &record, httplib::DataSink &sink,
                     std::size_t &cursor, bool &warned) {
        if (!warned && record->truncated()) {
            warned = true;
            const auto warning = nlohmann::json{{"type", "state"},
                                                {"state", std::string{to_string(record->state())}},
                                                {"truncated", true}};
            const auto frame = fmt::format("event: state\ndata: {}\n\n", warning.dump());
            if (!sink.write(frame.data(), frame.size())) {
                return false;
            }
        }

        const auto events = record->events_since(cursor, std::chrono::milliseconds{2000});
        for (const auto &event : events) {
            const auto frame = sse_frame(event);
            if (!sink.write(frame.data(), frame.size())) {
                return false;
            }
            cursor = event.sequence;
        }

        if (record->finished() && events.empty()) {
            sink.done();
            return false;
        }
        if (events.empty()) {
            // A comment frame: keeps an idle connection open through a proxy, and costs nothing.
            static constexpr std::string_view heartbeat = ": heartbeat\n\n";
            if (!sink.write(heartbeat.data(), heartbeat.size())) {
                return false;
            }
        }
        return true;
    }

    void summary(const httplib::Request &request, httplib::Response &response) {
        const auto record = runs_.find(request.matches[1].str());
        if (record == nullptr) {
            send_error(response, 404, "not_found",
                       fmt::format("no run called '{}'", request.matches[1].str()));
            return;
        }

        // Every CSV the run wrote, by output family: the whole-population one and one per income
        // category. Until this run the endpoint could only reduce the first, so the results screen
        // could not chart a stratified series at all — and neither could anything else, which is
        // part of why 45 columns of those files were empty for as long as they have existed
        // (docs/SUMMARY.md).
        const auto files = record->result_csvs();
        if (files.empty()) {
            send_error(response, 404, "not_found",
                       fmt::format("run {} has written no result CSV yet", record->id()));
            return;
        }

        auto family = std::string{"result"};
        if (request.has_param("family")) {
            family = request.get_param_value("family");
        }
        const auto chosen = files.find(family);
        if (chosen == files.end()) {
            std::vector<std::string> available;
            available.reserve(files.size());
            for (const auto &[name, unused] : files) {
                available.push_back(name);
            }
            send_error(response, 400, "bad_request",
                       fmt::format("run {} has no output family called '{}'; it wrote {}",
                                   record->id(), family, fmt::join(available, ", ")));
            return;
        }
        const auto &csv = chosen->second;

        SummaryFilter filter;
        if (request.has_param("sex")) {
            filter.sex = request.get_param_value("sex");
        }
        if (filter.sex != "all" && filter.sex != "male" && filter.sex != "female") {
            send_error(response, 400, "bad_request",
                       fmt::format("sex must be 'male', 'female' or 'all', not '{}'", filter.sex));
            return;
        }
        if (request.has_param("variable")) {
            std::istringstream names{request.get_param_value("variable")};
            std::string name;
            while (std::getline(names, name, ',')) {
                if (!name.empty()) {
                    filter.variables.push_back(name);
                }
            }
        }

        try {
            auto document = summarise_results(csv, filter);
            document["id"] = record->id();
            document["family"] = family;
            document["file"] = csv.filename().string();

            // Every family this run wrote, whichever one was asked for, so a client can offer the
            // selector without a second request.
            auto families = nlohmann::json::array();
            for (const auto &[name, path] : files) {
                families.push_back({{"family", name}, {"file", path.filename().string()}});
            }
            document["families"] = std::move(families);

            send_json(response, document);
        } catch (const std::exception &failure) {
            send_error(response, 500, "internal_error", failure.what());
        }
    }

    void result(const httplib::Request &request, httplib::Response &response) {
        const auto record = runs_.find(request.matches[1].str());
        if (record == nullptr) {
            send_error(response, 404, "not_found",
                       fmt::format("no run called '{}'", request.matches[1].str()));
            return;
        }
        const auto name = request.matches[2].str();
        const auto available = record->results();
        // A client names a run and a *file name*, never a path, and the name has to be one this
        // run actually wrote. There is nothing here to traverse.
        if (!is_plain_name(name) ||
            std::find(available.begin(), available.end(), name) == available.end()) {
            send_error(response, 404, "not_found",
                       fmt::format("run {} has no file called '{}'", record->id(), name));
            return;
        }

        const auto path = record->folder() / name;
        no_store(response);
        response.set_header("Content-Disposition",
                            fmt::format("attachment; filename=\"{}\"", name));
        response.set_content(read_file(path), media_type_of(path));
    }

    Options options_;
    RunStore runs_;
    httplib::Server server_;
    std::uint16_t port_{0};
};

// --- the handle ---------------------------------------------------------------------------------

Server::Server(Options options) : impl_{std::make_unique<Impl>(std::move(options))} {}
// stop(), not impl_->stop(): `thread_` is declared after `impl_` and so is destroyed first, and
// destroying a joinable std::thread calls std::terminate. A server started with `start()` and then
// simply dropped would have taken the process with it.
Server::~Server() { stop(); }

bool Server::listen() { return impl_->bind() && impl_->serve(); }

std::uint16_t Server::start() {
    if (!impl_->bind()) {
        return 0;
    }
    thread_ = std::thread([this] { impl_->serve(); });

    // Not just "the socket is bound": the port comes back from `bind`, and returning it the moment
    // the thread is spawned made `start()` mean "it will be serving shortly". A caller that then
    // stopped it straight away lost the stop and hung. `start()` now means what it says.
    impl_->wait_until_ready();
    return impl_->port();
}

void Server::stop() {
    impl_->stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::uint16_t Server::port() const noexcept { return impl_->port(); }

} // namespace hgps::server
