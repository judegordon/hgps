// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Input/model_parser.cpp (2,252 lines) and riskmodel.h, split here into
//         one unit per model family (docs/decisions/0019-split-the-monolith-translation-units.md).
#pragma once

#include "config/types.h"
#include "diagnostics/issue_report.h"
#include "model/demographic.h"
#include "model/mapping.h"
#include "model/riskfactor/hlm_model.h"
#include "model/riskfactor/risk_factor_model.h"
#include "model/riskfactor/kevin_hall/kevin_hall_model.h"
#include "model/riskfactor/static_linear/static_linear_model.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hgps::config::models {

/// @brief Everything a loaded model needs beyond its own JSON file.
struct LoadContext {
    /// @brief The declared risk factors, which every coefficient name is checked against.
    const model::HierarchicalMapping *mapping{};

    /// @brief The FactorsMean tables, shared by every adjusting model.
    std::shared_ptr<const model::SexAgeFactorTable> expected;

    /// @brief The config, for the files a model refers to and the project's requirements.
    const Config *config{};

    /// @brief The expected-value trend and its per-factor step counts, shared by every adjusting
    ///        model. Null when the project has no trend.
    std::shared_ptr<const std::map<core::Identifier, double>> trend;
    std::shared_ptr<const std::map<core::Identifier, int>> trend_steps;

    /// @brief Factors an already-loaded model generates that the config does not declare, which
    ///        count as known names for the models loaded after it.
    std::vector<core::Identifier> extra_factors;
};

/// @brief Region and ethnicity shares, read from the static model file and given to the
///        demographic module, which is what assigns them.
struct RegionEthnicityPrevalence {
    model::RegionPrevalence region;
    model::EthnicityPrevalence ethnicity;
};

/// @brief The two models a run needs, and what the static model's file said about the population.
struct RiskFactorModels {
    std::unique_ptr<model::RiskFactorModel> static_model;
    std::unique_ptr<model::RiskFactorModel> dynamic_model;

    /// @brief From the static model's `RegionFile` and `EthnicityFile`, when the project asks for
    ///        them. Empty otherwise.
    RegionEthnicityPrevalence prevalence;
};

/// @brief Loads the FactorsMean tables named by `modelling.baseline_adjustments`.
///
/// @return The table, or nullopt if a file could not be read or does not cover the age range.
std::shared_ptr<model::SexAgeFactorTable>
load_expected_values(const Config &config, diag::IssueReport &report);

/// @brief Loads both risk-factor model definitions.
///
/// Every coefficient name in every file is checked against the declared factor set before
/// anything is constructed, and an unknown name is an error naming the file, the JSON pointer and
/// the nearest known name (docs/decisions/0018-no-swallowing-catch-load-time-validation.md).
///
/// @return The models, or nullopt if any error was recorded.
std::optional<RiskFactorModels> load_risk_factor_models(const LoadContext &context,
                                                        diag::IssueReport &report);

/// @brief The `ModelName` a model file declares, lower-cased.
/// @return nullopt if the file could not be read or has no ModelName.
std::optional<std::string> read_model_name(const std::filesystem::path &path,
                                           diag::IssueReport &report);

namespace detail {

/// @brief Loads the `HLM` static model.
std::unique_ptr<model::RiskFactorModel> load_hlm(const nlohmann::json &document,
                                                  const std::filesystem::path &path,
                                                  const LoadContext &context,
                                                  diag::IssueReport &report);

/// @brief Loads the `EBHLM` dynamic model.
std::unique_ptr<model::RiskFactorModel> load_ebhlm(const nlohmann::json &document,
                                                    const std::filesystem::path &path,
                                                    const LoadContext &context,
                                                    diag::IssueReport &report);

/// @brief Loads the `StaticLinear` static model, in either its CSV-matrix or its JSON form.
std::unique_ptr<model::RiskFactorModel> load_static_linear(const nlohmann::json &document,
                                                            const std::filesystem::path &path,
                                                            const LoadContext &context,
                                                            diag::IssueReport &report);

/// @brief Loads the `KevinHall` dynamic model.
std::unique_ptr<model::RiskFactorModel> load_kevin_hall(const nlohmann::json &document,
                                                         const std::filesystem::path &path,
                                                         const LoadContext &context,
                                                         diag::IssueReport &report);

/// @brief Reads the static model's region and ethnicity prevalence files.
///
/// Only when `project_requirements.demographics` asks for them, and an error if it asks and the
/// file is not there — the alternative is a run that starts and then refuses at the first person.
std::optional<RegionEthnicityPrevalence>
load_region_and_ethnicity(const nlohmann::json &document, const std::filesystem::path &path,
                          const LoadContext &context, diag::IssueReport &report);

/// @brief Checks a coefficient name against the declared factors and the derived predictors.
/// @return true if it resolves; otherwise records a located error and returns false.
bool validate_predictor_name(const std::string &name, const std::filesystem::path &path,
                             const std::string &pointer, const LoadContext &context,
                             diag::IssueReport &report);

} // namespace detail

} // namespace hgps::config::models
