// The public API's implementation: four steps, each of which can fail with diagnostics rather
// than an exception.
//
// This file holds what used to be the body of `main()`. Moving it here is the whole point of the
// library/CLI split: the sequence of load, resolve, build and run is engine behaviour, it was
// duplicated once already in the test harness, and a second host — a GUI — would have duplicated
// it again (docs/decisions/0032-library-and-a-thin-cli.md).
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Console/program.cpp.
#include "hgps/engine.h"

#include "build_modules.h"
#include "diagnostics_bridge.h"
#include "manifest.h"
#include "perturbation.h"

#include "config/loader.h"
#include "config/types.h"
#include "core/parallel.h"
#include "data/store.h"
#include "io/data_source.h"
#include "output/result_writer.h"
#include "random/seed.h"
#include "sim/engine.h"

#include <string>
#include <utility>

#include <fmt/format.h>

namespace hgps::api {
namespace {

ScenarioKind to_public(sim::ScenarioType type) {
    return type == sim::ScenarioType::baseline ? ScenarioKind::baseline
                                               : ScenarioKind::intervention;
}

/// @brief The scenario names a configuration will produce, in the order they will run.
std::vector<std::string> scenario_names(const config::Config &config) {
    std::vector<std::string> names{"Baseline"};
    if (config.running.active_intervention.has_value()) {
        // The name the scenario objects report, so a subscriber's `scenario` field matches what
        // the run announced up front.
        names.emplace_back("Intervention");
    }
    return names;
}

} // namespace

std::string_view to_string(ScenarioKind kind) noexcept {
    return kind == ScenarioKind::baseline ? "baseline" : "intervention";
}

// --- Configuration ------------------------------------------------------------------------------

class Configuration::Impl {
  public:
    explicit Impl(config::Config config) : config_{std::move(config)} {}

    const config::Config &config() const noexcept { return config_; }

  private:
    config::Config config_;
};

Configuration::Configuration(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
Configuration::Configuration(Configuration &&) noexcept = default;
Configuration &Configuration::operator=(Configuration &&) noexcept = default;
Configuration::~Configuration() = default;

const std::filesystem::path &Configuration::path() const noexcept {
    return impl_->config().source_path;
}
const std::string &Configuration::sha256() const noexcept {
    return impl_->config().source_sha256;
}
std::uint32_t Configuration::seed() const noexcept { return impl_->config().running.seed; }
int Configuration::start_time() const noexcept {
    return static_cast<int>(impl_->config().running.start_time);
}
int Configuration::stop_time() const noexcept {
    return static_cast<int>(impl_->config().running.stop_time);
}
unsigned int Configuration::trial_runs() const noexcept {
    return impl_->config().running.trial_runs;
}

std::vector<std::string> Configuration::diseases() const {
    return impl_->config().running.diseases;
}

std::optional<std::string> Configuration::active_intervention() const {
    const auto &active = impl_->config().running.active_intervention;
    if (!active.has_value()) {
        return std::nullopt;
    }
    return active->identifier;
}

std::filesystem::path Configuration::output_folder() const {
    return std::filesystem::path{impl_->config().output.folder};
}

std::string Configuration::output_file_name() const {
    return config::expand_output_file_name(impl_->config().output, impl_->config().job_id);
}

const std::string &Configuration::data_source() const noexcept {
    return impl_->config().data.source;
}

std::optional<std::string> Configuration::data_checksum() const {
    return impl_->config().data.checksum;
}

const BaselineCompat &Configuration::baseline_compat() const noexcept {
    return impl_->config().baseline_compat;
}

// --- DataHandle ---------------------------------------------------------------------------------

class DataHandle::Impl {
  public:
    Impl(data::Store store, std::filesystem::path directory, std::optional<std::string> checksum)
        : store_{std::move(store)}, directory_{std::move(directory)},
          checksum_{std::move(checksum)} {}

    const data::Store &store() const noexcept { return store_; }
    const std::filesystem::path &directory() const noexcept { return directory_; }
    const std::optional<std::string> &checksum() const noexcept { return checksum_; }

  private:
    data::Store store_;
    std::filesystem::path directory_;
    std::optional<std::string> checksum_;
};

DataHandle::DataHandle(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
DataHandle::DataHandle(DataHandle &&) noexcept = default;
DataHandle &DataHandle::operator=(DataHandle &&) noexcept = default;
DataHandle::~DataHandle() = default;

const std::filesystem::path &DataHandle::directory() const noexcept { return impl_->directory(); }
std::optional<std::string> DataHandle::checksum() const { return impl_->checksum(); }

// --- Run ----------------------------------------------------------------------------------------

/// @brief Everything a run owns, in one heap allocation that never moves.
///
/// It never moves because the built modules hold references into `loaded` and into `journal`. That
/// is not an accident of this file: a module holding a reference to the disease definition map that
/// outlives it is how this implementation avoids the baseline's lazily-populated, concurrently
/// mutated repository (audit B-02). The price is that the owner has to be stable, and a
/// `unique_ptr<Impl>` behind a move-only handle is how that is paid.
class Run::Impl {
  public:
    explicit Impl(config::Config config) : config_{std::move(config)} {}

    config::Config config_;
    std::filesystem::path data_directory;
    engine::LoadedInputs loaded;
    sim::ScenarioJournal journal;
    sim::Modules baseline_modules;
    std::optional<sim::Modules> intervention_modules;
    Description description;
};

Run::Run(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
Run::Run(Run &&) noexcept = default;
Run &Run::operator=(Run &&) noexcept = default;
Run::~Run() = default;

const Run::Description &Run::description() const noexcept { return impl_->description; }

// --- the four steps -----------------------------------------------------------------------------

std::optional<Configuration> load_configuration(const std::filesystem::path &path,
                                                const LoadOptions &options, Report &report) {
    diag::IssueReport internal;

    config::LoadOptions internal_options;
    internal_options.output_folder = options.output_folder;
    internal_options.output_folder_override = options.output_folder_override;
    internal_options.job_id = options.job_id;
    internal_options.verbose = options.verbose;
    internal_options.require_files_exist = options.require_files_exist;
    internal_options.baseline_compat = options.baseline_compat;

    auto config = config::load(path, internal_options, internal);
    detail::append_to_public(internal, report);

    if (!config.has_value()) {
        return std::nullopt;
    }
    return Configuration{std::make_unique<Configuration::Impl>(std::move(*config))};
}

std::optional<DataHandle> resolve_data(const Configuration &configuration, Report &report) {
    const auto &config = configuration.impl().config();
    diag::IssueReport internal;

    const io::DataSource source{config.data.source, config.data.checksum, config.root_path};
    const auto directory = source.resolve(internal);
    if (!directory.has_value()) {
        detail::append_to_public(internal, report);
        return std::nullopt;
    }

    auto store = data::Store::open(*directory, internal);
    const bool failed = !store.has_value() || internal.has_errors();
    detail::append_to_public(internal, report);
    if (failed) {
        return std::nullopt;
    }

    return DataHandle{std::make_unique<DataHandle::Impl>(std::move(*store), *directory,
                                                         config.data.checksum)};
}

std::optional<Run> build_run(const Configuration &configuration, const DataHandle &data,
                             Report &report) {
    const auto &config = configuration.impl().config();
    diag::IssueReport internal;

    // Constructed first and filled in place: the modules built below hold references into
    // `impl->loaded` and `impl->journal`, so neither may be moved after they are built.
    auto impl = std::make_unique<Run::Impl>(config);
    impl->data_directory = data.impl().directory();

    auto loaded = engine::load_inputs(impl->config_, data.impl().store(), internal);
    if (!loaded.has_value()) {
        detail::append_to_public(internal, report);
        return std::nullopt;
    }
    impl->loaded = std::move(*loaded);

    auto baseline_modules =
        engine::build_modules(impl->loaded, impl->config_, impl->journal, internal);
    if (!baseline_modules.has_value()) {
        detail::append_to_public(internal, report);
        return std::nullopt;
    }
    impl->baseline_modules = std::move(*baseline_modules);

    if (impl->config_.running.active_intervention.has_value()) {
        auto intervention_modules =
            engine::build_modules(impl->loaded, impl->config_, impl->journal, internal);
        if (!intervention_modules.has_value()) {
            detail::append_to_public(internal, report);
            return std::nullopt;
        }
        impl->intervention_modules = std::move(*intervention_modules);
    }

    impl->description = Run::Description{
        .country = impl->loaded.inputs->settings().country.name,
        .disease_count = impl->loaded.inputs->diseases().size(),
        .risk_factor_count = impl->loaded.inputs->risk_mapping().size(),
        .cohort_size = impl->loaded.cohort_size,
        .start_time = static_cast<int>(impl->config_.running.start_time),
        .stop_time = static_cast<int>(impl->config_.running.stop_time),
        .trial_runs = impl->config_.running.trial_runs,
        .seed = impl->config_.running.seed,
        .run_seeds = {},
        .scenarios = scenario_names(impl->config_),
    };
    for (unsigned int run = 0; run < impl->config_.running.trial_runs; ++run) {
        impl->description.run_seeds.push_back(
            rng::derive_run_seed(impl->config_.running.seed, run));
    }

    // Warnings from a successful build are worth seeing, and the caller reads the report either
    // way — which is why this is not inside the failure branches above.
    detail::append_to_public(internal, report);
    return Run{std::move(impl)};
}

RunSummary execute(Run &run, const RunOptions &options, EventSubscriber *subscriber,
                   const CancellationToken &cancellation, Report &report) {
    auto &impl = run.impl();
    const auto &config = impl.config_;
    const auto &description = impl.description;

    // Set once, before anything runs a parallel region. Scoped, so a host that calls execute()
    // twice with different thread counts gets what it asked for each time.
    const core::parallel::WorkerCountScope workers{options.threads};

    engine::Manifest manifest;
    manifest.config_path = config.source_path;
    manifest.config_sha256 = config.source_sha256;
    manifest.data_source = config.data.source;
    manifest.data_checksum = config.data.checksum.value_or("");
    manifest.seed = description.seed;
    manifest.run_seeds = description.run_seeds;
    manifest.engine_version = std::string{build_info().version};
    manifest.git_commit = std::string{build_info().git_commit};
    manifest.git_describe = std::string{build_info().git_describe};
    manifest.git_dirty = build_info().git_dirty;
    manifest.platform = std::string{build_info().platform};
    manifest.compiler = std::string{build_info().compiler};
    manifest.build_type = std::string{build_info().build_type};
    manifest.started_utc = engine::utc_timestamp_now();
    manifest.scenarios = description.scenarios;
    manifest.country = description.country;
    manifest.start_time = description.start_time;
    manifest.stop_time = description.stop_time;
    manifest.trial_runs = description.trial_runs;
    manifest.cohort_size = description.cohort_size;
    manifest.threads = options.threads;
    manifest.baseline_compat = config.baseline_compat.names();

    output::RunMetadata metadata;
    metadata.model = "healthgps";
    metadata.version = std::string{build_info().version};
    metadata.intervention = config.running.active_intervention.has_value()
                                ? config.running.active_intervention->identifier
                                : "";
    metadata.job_id = config.job_id;
    metadata.seed = description.seed;
    metadata.run_seeds = description.run_seeds;
    metadata.config_path = config.source_path;
    metadata.config_sha256 = config.source_sha256;
    metadata.country = description.country;
    metadata.start_time = config.running.start_time;
    metadata.stop_time = config.running.stop_time;
    metadata.trial_runs = config.running.trial_runs;
    metadata.cohort_size = description.cohort_size;

    const auto output_path = std::filesystem::path{config.output.folder} /
                             config::expand_output_file_name(config.output, config.job_id);

    RunSummary summary;
    summary.seed = description.seed;

    // Parsed before anything runs, because a specification that does not parse must not produce an
    // unperturbed run: the test that uses this asserts a failure, and a silent no-op would make it
    // pass for the wrong reason (ADR 0036).
    engine::Perturbation perturbation;
    if (!options.perturbation.empty()) {
        std::string error;
        auto parsed = engine::Perturbation::parse(options.perturbation, error);
        if (!parsed.has_value()) {
            report.add(Diagnostic{.severity = Severity::error,
                                  .code = "config_bad_value",
                                  .location = Location{.field = "perturbation"},
                                  .message = fmt::format("{}. The form is 'channel=op:value', "
                                                         "separated by ';', with 'scale' and 'step' "
                                                         "as the operations",
                                                         error)});
            return summary;
        }
        perturbation = std::move(*parsed);
        manifest.perturbation = options.perturbation;
    }

    if (subscriber != nullptr) {
        const auto years_per_scenario =
            static_cast<std::size_t>(description.stop_time - description.start_time) + 1;
        subscriber->on_run_started(
            RunStarted{.engine_version = std::string{build_info().version},
                       .seed = description.seed,
                       .trial_runs = description.trial_runs,
                       .start_time = description.start_time,
                       .stop_time = description.stop_time,
                       .cohort_size = description.cohort_size,
                       .scenarios = description.scenarios,
                       .total_years = years_per_scenario * description.scenarios.size() *
                                      description.trial_runs});
    }

    sim::RunHooks hooks;
    if (subscriber != nullptr) {
        hooks.scenario_started = [subscriber](sim::ScenarioType type, const std::string &name,
                                             unsigned int run_number) {
            subscriber->on_scenario_started(
                ScenarioStarted{.scenario = name, .kind = to_public(type), .run = run_number});
        };
        hooks.year_completed = [subscriber](sim::ScenarioType type, const std::string &name,
                                           unsigned int run_number, int year, double elapsed_ms,
                                           std::size_t population_size) {
            subscriber->on_year_completed(YearCompleted{.scenario = name,
                                                        .kind = to_public(type),
                                                        .run = run_number,
                                                        .year = year,
                                                        .elapsed_ms = elapsed_ms,
                                                        .population_size = population_size});
        };
        hooks.scenario_completed = [subscriber](sim::ScenarioType type, const std::string &name,
                                               unsigned int run_number, double elapsed_ms,
                                               std::size_t years) {
            subscriber->on_scenario_completed(ScenarioCompleted{.scenario = name,
                                                                .kind = to_public(type),
                                                                .run = run_number,
                                                                .elapsed_ms = elapsed_ms,
                                                                .years_completed = years});
        };
    }
    hooks.cancelled = [&cancellation]() { return cancellation.cancelled(); };

    {
        output::ResultWriter writer{output_path, metadata,
                                    impl.loaded.inputs->income_analysis_enabled(),
                                    impl.loaded.inputs->income_layout()};

        sim::Engine baseline{impl.loaded.inputs, std::make_unique<sim::BaselineScenario>(),
                             std::move(impl.baseline_modules), config.running.seed};

        sim::Runner runner;

        // The ordinary path is a straight hand-over. The copy happens only for a perturbed run, which
        // is a test, so the cost of copying a year's results is nobody's problem.
        const auto sink = [&writer, &perturbation](const sim::ResultRow &row) {
            if (perturbation.empty()) {
                writer.write(row);
                return;
            }
            auto corrupted = row;
            perturbation.apply(corrupted.result);
            writer.write(corrupted);
        };

        sim::Runner::Outcome outcome;
        if (impl.intervention_modules.has_value()) {
            sim::Engine intervention{
                impl.loaded.inputs,
                sim::create_intervention_scenario(*config.running.active_intervention,
                                                  config.baseline_compat),
                std::move(*impl.intervention_modules), config.running.seed};

            outcome = runner.run(baseline, intervention, config.running.trial_runs,
                                 config.running.seed, sink, &hooks);
        } else {
            outcome = runner.run(baseline, config.running.trial_runs, config.running.seed, sink,
                                 &hooks);
        }

        summary.elapsed_ms = outcome.elapsed_ms;
        summary.cancelled = outcome.cancelled;
        summary.years_completed = outcome.years_completed;
        manifest.warnings = outcome.warnings;
        summary.result_csv = writer.csv_path();
        summary.result_json = writer.json_path();
        summary.outputs = writer.paths();
    }

    manifest.finished_utc = engine::utc_timestamp_now();
    manifest.elapsed_ms = summary.elapsed_ms;
    manifest.cancelled = summary.cancelled;
    manifest.years_completed = summary.years_completed;
    manifest.data_directory = impl.data_directory;
    for (const auto &path : summary.outputs) {
        manifest.results.push_back(path.filename().string());
    }

    if (options.write_manifest) {
        auto manifest_path = output_path;
        manifest_path.replace_extension();
        manifest_path += "_manifest.json";
        engine::write_manifest(manifest_path, manifest);
        summary.manifest = manifest_path;
        summary.outputs.push_back(manifest_path);
    }

    // A rule that never fired is a channel the output does not have — a typo, almost always. The run
    // happened and its files are written, but it did not do what it was asked, so it is not a success.
    if (const auto missed = perturbation.rules_that_never_fired(); !missed.empty()) {
        std::string names;
        for (const auto &name : missed) {
            if (!names.empty()) {
                names += ", ";
            }
            names += name;
        }
        report.add(Diagnostic{
            .severity = Severity::error,
            .code = "config_bad_value",
            .location = Location{.field = "perturbation"},
            .message = fmt::format("the perturbation names channel(s) this run's output does not "
                                   "have, so they were never perturbed: {}",
                                   names)});
        return summary;
    }

    summary.succeeded = true;

    if (subscriber != nullptr) {
        subscriber->on_run_completed(RunCompleted{.elapsed_ms = summary.elapsed_ms,
                                                  .cancelled = summary.cancelled,
                                                  .outputs = summary.outputs});
    }

    // Nothing accumulates into the report during a run today: every diagnostic the engine can
    // raise is raised while loading, which is the design (ADR 0018). The parameter is here because
    // a run that discovers something — the emptying-band shortfall is the candidate — must have
    // somewhere to put it that is not stdout.
    (void)report;

    return summary;
}

RunSummary execute(Run &run, const RunOptions &options, Report &report) {
    const CancellationToken never_cancelled;
    return execute(run, options, nullptr, never_cancelled, report);
}

} // namespace hgps::api
