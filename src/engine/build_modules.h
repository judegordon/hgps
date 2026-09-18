// Turns a validated config and an open data store into the five modules a run needs.
//
// Engine internals, not the public API: `include/hgps/engine.h` is what a caller sees, and
// `build_run` is this file's one public face
// (docs/decisions/0032-library-and-a-thin-cli.md).
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: the build_*_module functions and src/HealthGPS/converter.cpp, and the wiring in
//         src/HealthGPS.Console/program.cpp.
#pragma once

#include "config/models/model_loader.h"
#include "config/types.h"
#include "data/store.h"
#include "diagnostics/issue_report.h"
#include "model/disease/disease_table.h"
#include "model/model_input.h"
#include "sim/engine.h"

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace hgps::engine {

/// @brief Everything a run needs, loaded once, before any scenario starts.
///
/// Loading up front is a deliberate difference from the baseline, whose repository loads disease
/// definitions lazily from inside a parallel loop behind a lock-free fast path that races a
/// concurrent insert (audit B-02, confirmed by ThreadSanitizer). There is no lazy load here to
/// race.
struct LoadedInputs {
    std::shared_ptr<const model::ModelInput> inputs;

    /// @brief One definition per selected disease, keyed by code and owned here — the models hold
    ///        references into this map, which outlives them.
    std::map<core::Identifier, model::DiseaseDefinition> diseases;

    model::LmsDefinition lms;
    std::shared_ptr<const model::SexAgeFactorTable> expected;

    /// @brief The expected-value trend and its per-factor step counts, when the project has a
    ///        trend. Null otherwise, which is the same as "no trend" to every model.
    std::shared_ptr<const std::map<core::Identifier, double>> trend;
    std::shared_ptr<const std::map<core::Identifier, int>> trend_steps;
    std::optional<model::AnalysisDefinition> analysis;

    /// @brief The population series and life table the demographic module needs.
    std::map<int, std::map<int, model::PopulationRecord>> population_data;
    std::optional<model::LifeTable> life_table;

    std::size_t cohort_size{};
};

/// @brief Loads the input dataset, the data store's tables and the model definitions.
/// @return The loaded inputs, or nullopt if any error was recorded.
std::optional<LoadedInputs> load_inputs(const config::Config &config, const data::Store &store,
                                        diag::IssueReport &report);

/// @brief Builds one scenario's modules. Called once per scenario, so each gets its own state.
std::optional<sim::Modules> build_modules(const LoadedInputs &loaded,
                                          const config::Config &config,
                                          sim::ScenarioJournal &journal,
                                          diag::IssueReport &report);

} // namespace hgps::engine
