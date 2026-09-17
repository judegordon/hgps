#include "model_loader.h"

#include "core/string_util.h"
#include "io/csv_reader.h"
#include "io/json.h"
#include "model/predictor_resolver.h"

#include <utility>

#include <fmt/format.h>

namespace hgps::config::models {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

} // namespace

namespace detail {

bool validate_predictor_name(const std::string &name, const std::filesystem::path &path,
                             const std::string &pointer, const LoadContext &context,
                             diag::IssueReport &report) {
    const auto factors = context.mapping->keys();
    if (model::is_resolvable_predictor(name, factors)) {
        return true;
    }

    // The mistake this exists for: a misspelled coefficient name. The earlier rewrite swallowed
    // it and substituted an expected value, so the run produced plausible numbers (audit R-05).
    const auto suggestions = model::nearest_predictor_names(name, factors);
    report.error(IssueCode::model_unknown_predictor,
                 IssueLocation{.file = path.string(), .field = pointer},
                 suggestions.empty()
                     ? fmt::format("'{}' is not a declared risk factor or a derived predictor",
                                   name)
                     : fmt::format("'{}' is not a declared risk factor or a derived predictor; "
                                   "did you mean {}?",
                                   name, fmt::join(suggestions, " or ")));
    return false;
}

} // namespace detail

std::shared_ptr<model::SexAgeFactorTable>
load_expected_values(const Config &config, diag::IssueReport &report) {
    const auto &adjustments = config.modelling.baseline_adjustments;

    if (!core::case_insensitive::equals(adjustments.format, "csv")) {
        report.error(IssueCode::config_bad_value,
                     IssueLocation{.field = "/modelling/baseline_adjustments/format"},
                     fmt::format("unsupported file format '{}'; only csv is supported",
                                 adjustments.format));
        return nullptr;
    }

    io::CsvOptions options;
    if (!adjustments.delimiter.empty()) {
        options.delimiter = adjustments.delimiter.front();
    }

    auto table = std::make_shared<model::SexAgeFactorTable>();
    const auto max_age = static_cast<std::size_t>(config.settings.age_range.upper());

    const std::vector<std::pair<core::Gender, std::string>> files{
        {core::Gender::male, "factorsmean_male"}, {core::Gender::female, "factorsmean_female"}};

    for (const auto &[gender, role] : files) {
        const auto found = adjustments.file_names.find(role);
        if (found == adjustments.file_names.end()) {
            report.error(IssueCode::config_missing_required,
                         IssueLocation{.field = fmt::format(
                                           "/modelling/baseline_adjustments/file_names/{}", role)},
                         "required");
            continue;
        }

        const auto values = io::load_baseline_adjustments_from_csv(found->second, options, report);
        if (!values.has_value()) {
            continue;
        }

        for (const auto &[factor, by_age] : *values) {
            if (by_age.size() <= max_age) {
                report.error(IssueCode::csv_bad_value,
                             IssueLocation{.file = found->second.string(),
                                           .field = factor.to_string()},
                             fmt::format("the FactorsMean table covers {} ages, but the "
                                         "configured age range needs {}",
                                         by_age.size(), max_age + 1));
                continue;
            }
            table->emplace(gender, factor, by_age);
        }
    }

    if (table->empty()) {
        return nullptr;
    }

    return table;
}

namespace {

/// The members of a fitted-model file that the simulation never reads.
///
/// `residuals` and `fittedValues` are per-observation diagnostics from the R fit — 40,000 numbers
/// each, per factor. France's static_model.json is 18.8 MB of text and almost all of it is these,
/// so they are dropped during the parse rather than after it (see io::read_json).
const std::vector<std::string> kUnusedModelMembers{"residuals", "fittedValues"};

/// @brief Reads a model file once, skipping the members nothing reads.
std::optional<nlohmann::json> read_model_document(const std::filesystem::path &path,
                                                  diag::IssueReport &report) {
    return io::read_json(path, report, kUnusedModelMembers);
}

/// @brief The lower-cased `ModelName` of an already-parsed model document.
std::optional<std::string> model_name_of(const nlohmann::json &document,
                                         const std::filesystem::path &path,
                                         diag::IssueReport &report) {
    if (!document.contains("ModelName") || !document["ModelName"].is_string()) {
        report.error(IssueCode::model_missing_key,
                     IssueLocation{.file = path.string(), .field = "/ModelName"},
                     "a risk-factor model file must declare a string 'ModelName'");
        return std::nullopt;
    }

    return core::to_lower(document["ModelName"].get<std::string>());
}

} // namespace

std::optional<std::string> read_model_name(const std::filesystem::path &path,
                                           diag::IssueReport &report) {
    const auto document = read_model_document(path, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    return model_name_of(*document, path, report);
}

std::optional<RiskFactorModels> load_risk_factor_models(const LoadContext &context,
                                                        diag::IssueReport &report) {
    const auto before = report.error_count();

    const auto &models = context.config->modelling.risk_factor_models;
    RiskFactorModels result;

    const auto load_one = [&](const std::string &slot) -> std::unique_ptr<model::RiskFactorModel> {
        const auto found = models.find(slot);
        if (found == models.end()) {
            report.error(IssueCode::config_missing_required,
                         IssueLocation{.field = fmt::format("/modelling/risk_factor_models/{}",
                                                            slot)},
                         "required");
            return nullptr;
        }

        const auto &path = found->second;

        // Once, not twice: reading the file to find its ModelName and then again to load it
        // doubled both the parse time and the memory spike on an 18.8 MB model file.
        const auto document = read_model_document(path, report);
        if (!document.has_value()) {
            return nullptr;
        }

        const auto name = model_name_of(*document, path, report);
        if (!name.has_value()) {
            return nullptr;
        }

        // Only the model families this build implements. A name it does not know stops the run
        // with a sentence rather than being ignored (ADR 0021).
        if (slot == "static") {
            if (*name == "hlm") {
                return detail::load_hlm(*document, path, context, report);
            }
            report.error(IssueCode::feature_not_implemented,
                         IssueLocation{.file = path.string(), .field = "/ModelName"},
                         fmt::format("static model '{}' is not implemented in this build; this "
                                     "build implements 'HLM'. See docs/backlog.md",
                                     *name));
            return nullptr;
        }

        if (*name == "ebhlm") {
            return detail::load_ebhlm(*document, path, context, report);
        }

        report.error(IssueCode::feature_not_implemented,
                     IssueLocation{.file = path.string(), .field = "/ModelName"},
                     fmt::format("dynamic model '{}' is not implemented in this build; this build "
                                 "implements 'EBHLM'. See docs/backlog.md",
                                 *name));
        return nullptr;
    };

    result.static_model = load_one("static");
    result.dynamic_model = load_one("dynamic");

    if (report.error_count() != before || !result.static_model || !result.dynamic_model) {
        return std::nullopt;
    }

    return result;
}

} // namespace hgps::config::models
