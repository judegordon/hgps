// Config v2. Derived in shape from Health-GPS's v1 config
// (BSD-3-Clause, Imperial College London / INRAE; see LICENSE), so that conversion from an
// upstream config is mechanical and a reader who knows that format knows this one.
// docs/decisions/0010-config-v2-and-a-converter.md records what changed and why.
#pragma once

#include "hgps/baseline_compat.h"

#include "core/interval.h"
#include "core/types.h"
#include "io/csv_reader.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hgps::config {

/// @brief The config format version this build reads. Identified for certain by `$schema`.
inline constexpr int kConfigVersion = 2;

/// @brief Where the back-end data store comes from.
struct DataSpec {
    std::string source;

    /// @brief Required when `source` is a URL or a .zip (ADR 0011). Optional for a directory.
    std::optional<std::string> checksum;

    auto operator<=>(const DataSpec &) const = default;
};

/// @brief The survey microdata file and the types of the columns to read from it.
struct DatasetFile {
    std::filesystem::path name;
    std::string format{"csv"};
    std::string delimiter{","};
    std::string encoding{"ASCII"};
    std::vector<io::CsvColumnSpec> columns;
};

/// @brief The population the experiment draws.
struct Settings {
    std::string country_code;
    core::IntegerInterval age_range{};
    double size_fraction{};
};

/// @brief The socio-economic status noise model.
struct SesModel {
    std::string function_name;
    std::vector<double> function_parameters;

    auto operator<=>(const SesModel &) const = default;
};

/// @brief One income stratum's pair of FactorsMean tables.
struct IncomeStratumEntry {
    std::string id;
    std::filesystem::path factorsmean_male;
    std::filesystem::path factorsmean_female;

    auto operator<=>(const IncomeStratumEntry &) const = default;
};

/// @brief Optional per-stratum FactorsMean adjustment (FINCH uses quintiles).
struct IncomeStratumFactorsMean {
    bool enabled{false};

    /// @brief The number of rank buckets. When enabled it must equal strata.size().
    std::size_t adjustment_income_stratum_count{0};

    std::vector<IncomeStratumEntry> strata;

    auto operator<=>(const IncomeStratumFactorsMean &) const = default;
};

/// @brief The FactorsMean tables the risk-factor models calibrate against.
struct BaselineAdjustments {
    std::string format{"csv"};
    std::string delimiter{","};
    std::string encoding{"ASCII"};

    /// @brief Keyed by role: "factorsmean_male", "factorsmean_female".
    std::map<std::string, std::filesystem::path> file_names;

    IncomeStratumFactorsMean income_stratum_factors_mean;
};

/// @brief One risk factor: its name, the level it is generated at, and its plausible range.
struct RiskFactorSpec {
    std::string name;
    int level{};
    std::optional<core::DoubleInterval> range;

    auto operator<=>(const RiskFactorSpec &) const = default;
};

/// @brief The models and their parameters.
struct Modelling {
    SesModel ses_model;

    /// @brief The calendar year policies start. 0 means "start_time + 2", as upstream.
    unsigned int policy_start_year{0};

    std::vector<RiskFactorSpec> risk_factors;

    /// @brief "static" and "dynamic" to their model definition JSON files.
    std::map<std::string, std::filesystem::path> risk_factor_models;

    BaselineAdjustments baseline_adjustments;

    /// @brief The `demographic_models` block, kept as JSON: its shape belongs to the risk-factor
    ///        model that consumes it, and config validation has no business knowing it.
    nlohmann::json demographic_models = nlohmann::json::object();
};

struct PolicyPeriod {
    int start_time{};
    std::optional<int> finish_time;

    auto operator<=>(const PolicyPeriod &) const = default;
};

struct PolicyImpact {
    std::string risk_factor;
    double impact_value{};
    unsigned int from_age{};
    std::optional<unsigned int> to_age;

    auto operator<=>(const PolicyImpact &) const = default;
};

struct PolicyAdjustment {
    std::string risk_factor;
    double value{};

    auto operator<=>(const PolicyAdjustment &) const = default;
};

/// @brief One intervention scenario's parameters.
struct InterventionSpec {
    std::string identifier;
    PolicyPeriod active_period;
    std::string impact_type;
    std::vector<PolicyImpact> impacts;
    std::vector<double> dynamics;
    std::vector<double> coefficients;
    std::vector<double> coverage_rates;
    std::optional<unsigned int> coverage_cutoff_time;
    std::optional<unsigned int> child_cutoff_age;
    std::vector<PolicyAdjustment> adjustments;

    auto operator<=>(const InterventionSpec &) const = default;
};

/// @brief The experiment's horizon, seed and diseases.
struct Running {
    /// @brief Required, and a scalar. There is no code path here that runs unseeded (ADR 0015).
    std::uint32_t seed{};

    unsigned int start_time{};
    unsigned int stop_time{};
    unsigned int trial_runs{1};
    std::vector<std::string> diseases;

    /// @brief The intervention named by `interventions.active_type_id`, if any.
    std::optional<InterventionSpec> active_intervention;
};

/// @brief Filters for the optional per-person tracking output.
struct IndividualTracking {
    bool enabled{false};
    std::optional<int> age_min;
    std::optional<int> age_max;
    std::string gender{"all"};      // "male" | "female" | "all"
    std::vector<std::string> regions;
    std::vector<std::string> ethnicities;
    std::vector<std::string> risk_factors; // empty = all
    std::vector<int> years;                // empty = all
    std::string scenarios{"both"};         // "baseline" | "intervention" | "both"

    auto operator<=>(const IndividualTracking &) const = default;
};

/// @brief Where results go.
struct Output {
    unsigned int comorbidities{};
    std::string folder;

    /// @brief Used exactly as configured. `{TIMESTAMP}` is an optional token, not a requirement
    ///        (the baseline ignores this name unless it contains one — audit B-08).
    std::string file_name;

    std::optional<IndividualTracking> individual_id_tracking;
};

/// @brief Which parts of the model this project uses. Required in config v2, because the upstream
///        behaviour gated on it is otherwise reachable only by accident (audit D-03).
struct ProjectRequirements {
    struct Demographics {
        bool age{true};
        bool gender{true};
        bool region{false};
        bool ethnicity{false};

        /// @brief Cap age at this value in linear models when set and positive.
        std::optional<int> max_age_for_linear_models;

        /// @brief Which sex takes value 1 for the `gender2` predictor row.
        std::string gender2{"male"};

        auto operator<=>(const Demographics &) const = default;
    } demographics;

    struct Income {
        bool enabled{true};
        std::string type{"categorical"}; // "continuous" | "categorical"
        std::string categories{"3"};     // "3" | "4" | "5"
        bool adjust_to_factors_mean{false};
        bool trended{false};
        bool income_based_csv_output{true};

        auto operator<=>(const Income &) const = default;
    } income;

    struct PhysicalActivity {
        bool enabled{true};
        std::string type{"simple"}; // "simple" | "continuous"
        bool adjust_to_factors_mean{false};
        bool trended{false};

        auto operator<=>(const PhysicalActivity &) const = default;
    } physical_activity;

    struct RiskFactors {
        bool adjust_to_factors_mean{true};
        bool trended{true};

        auto operator<=>(const RiskFactors &) const = default;
    } risk_factors;

    struct Trend {
        bool enabled{false};
        std::string type{"null"}; // "null" | "trend" | "upf_trend" | "income_trend"

        auto operator<=>(const Trend &) const = default;
    } trend;

    struct TwoStage {
        bool use_logistic{false};
        std::string logistic_file;

        auto operator<=>(const TwoStage &) const = default;
    } two_stage;

    auto operator<=>(const ProjectRequirements &) const = default;
};

/// @brief `population_impact_fraction`: which PIF tables a run applies, if any.
///
/// A PIF multiplies the intervention scenario's disease incidence by (1 − fraction). It is a third
/// policy mechanism, alongside the age-banded `running.interventions` block and the static linear
/// model's `policy_start_year` coefficients, and like the latter it does not go through
/// `Scenario::apply` ([ADR 0038](../../docs/decisions/0038-population-impact-fraction.md)).
struct PopulationImpactFraction {
    bool enabled{false};

    /// @brief The risk factor the fractions were estimated for, as the data tree names it —
    ///        `Smoking`, `Alcohol`. A directory name, so its spelling and case are the tree's and not
    ///        an identifier's.
    std::string risk_factor;

    /// @brief Which modelled policy scenario to read: `Scenario1`, `Scenario2` or `Scenario3` in the
    ///        published pack. Also a directory name.
    std::string scenario;

    auto operator<=>(const PopulationImpactFraction &) const = default;
};

/// @brief A whole config v2 document, validated.
struct Config {
    /// @brief The directory the config was loaded from; relative paths resolve against it.
    std::filesystem::path root_path;

    /// @brief The config file itself, for the run metadata.
    std::filesystem::path source_path;

    /// @brief The SHA-256 of the config file, recorded in the run metadata so a result can be
    ///        tied to the exact inputs that produced it.
    std::string source_sha256;

    int version{kConfigVersion};
    ProjectRequirements project_requirements;
    DataSpec data;
    DatasetFile dataset;
    Settings settings;
    Modelling modelling;
    Running running;
    Output output;
    PopulationImpactFraction population_impact_fraction;

    /// @brief The deliberate deviations this run puts back, if any. Empty by default: the fixed
    ///        behaviour is what this project stands behind, and a flag here is a request to
    ///        reproduce a baseline defect so its effect can be measured
    ///        ([ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)).
    ///
    /// The union of the config document's `baseline_compat` array and whatever the caller asked
    /// for — neither overrides the other, because both are requests to restore a behaviour.
    api::BaselineCompat baseline_compat;

    core::VerboseMode verbosity{core::VerboseMode::none};
    int job_id{0};
};

} // namespace hgps::config
