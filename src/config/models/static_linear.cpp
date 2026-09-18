// The `StaticLinear` static model: two CSV matrices, three CSV coefficient tables or a JSON block
// per factor, an income model, a physical-activity model, and the region and ethnicity prevalence
// the demographic module assigns from.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: load_staticlinear_risk_model_definition and register_risk_factor_model_definitions in
//         src/HealthGPS.Input/model_parser.cpp.
#include "model_loader.h"

#include "core/income_category_layout.h"
#include "core/string_util.h"
#include "io/csv_reader.h"
#include "io/json.h"
#include "model/predictor_resolver.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace hgps::config::models::detail {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

/// The rows of a coefficient CSV that are parameters of the model rather than predictors.
bool is_parameter_row(const std::string &name) {
    return core::case_insensitive::equals(name, "intercept") ||
           core::case_insensitive::equals(name, "lambda") ||
           core::case_insensitive::equals(name, "stddev") ||
           core::case_insensitive::equals(name, "min") ||
           core::case_insensitive::equals(name, "max");
}

/// The policy CSV spells the logged energy-intake predictor three ways across the packs —
/// `EnergyIntake`, `log_EnergyIntake` and `log_energy_intake` — and all three mean the log of the
/// person's energy intake. They are canonicalised to `log_energyintake`, which is the spelling
/// the derived-predictor resolver understands: `log_` plus the name of a declared risk factor.
///
/// The baseline canonicalises to `log_energy_intake` instead and then special-cases that one name
/// in the evaluator's fallback. Choosing the spelling the resolver already handles means the name
/// is validated at load time like every other, rather than being a name only one code path knows.
std::string canonical_predictor_row(const std::string &raw) {
    if (core::case_insensitive::equals(raw, "EnergyIntake") ||
        core::case_insensitive::equals(raw, "log_energyintake") ||
        core::case_insensitive::equals(raw, "log_EnergyIntake") ||
        core::case_insensitive::equals(raw, "log_energy_intake")) {
        return "log_energyintake";
    }
    return raw;
}

/// @brief A `{name, format, delimiter, encoding}` file block, resolved against the config root.
struct FileBlock {
    std::filesystem::path path;
    io::CsvOptions options;
};

std::optional<FileBlock> read_file_block(const io::JsonCursor &cursor, const std::string &field,
                                         const std::filesystem::path &root,
                                         const std::string &name_key = "name") {
    const auto block = cursor.object(field);
    if (!block.has_value()) {
        return std::nullopt;
    }

    block->reject_unknown_members({name_key, "format", "delimiter", "encoding", "columns"});

    const auto name = block->string(name_key);
    if (!name.has_value()) {
        return std::nullopt;
    }

    const auto format = block->string_or_default("format", "csv");
    if (!core::case_insensitive::equals(format, "csv")) {
        block->error("format", IssueCode::config_bad_value,
                     fmt::format("unsupported file format '{}'; only csv is supported", format));
        return std::nullopt;
    }

    FileBlock result;
    // Relative to the *model file's* directory, not to the config's. They are the same directory
    // in an upstream example, but a converted config lives in this repository while the model
    // files it names stay upstream — and the names inside a model file are that file's own
    // relative references, which is what makes this the reading that is right in both layouts.
    result.path = std::filesystem::path{*name}.is_absolute() ? std::filesystem::path{*name}
                                                             : root / *name;
    const auto delimiter = block->string_or_default("delimiter", ",");
    result.options.delimiter = delimiter == "\\t"  ? '\t'
                               : delimiter.empty() ? ','
                                                   : delimiter.front();
    return result;
}

/// @brief A square matrix CSV whose columns are the risk factors.
struct MatrixCsv {
    std::vector<core::Identifier> names;
    core::Matrix values;
};

/// Reads a square matrix whose header row names the factors. `has_row_labels` says whether the
/// first column is a row name rather than data — the correlation file has one and the policy
/// covariance file does not, which is a difference in the upstream data and not a choice here.
std::optional<MatrixCsv> read_matrix_csv(const FileBlock &file, bool has_row_labels,
                                          const std::string &what, diag::IssueReport &report) {
    const auto document = io::read_csv(file.path, file.options, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const std::size_t first_column = has_row_labels ? 1 : 0;
    if (document->num_columns() <= first_column) {
        report.error(IssueCode::csv_missing_column, IssueLocation{.file = file.path.string()},
                     fmt::format("the {} has no factor columns", what));
        return std::nullopt;
    }

    MatrixCsv result;
    for (std::size_t column = first_column; column < document->num_columns(); ++column) {
        result.names.emplace_back(document->headers()[column]);
    }

    const auto size = result.names.size();
    if (document->num_rows() != size) {
        report.error(IssueCode::model_dimension_mismatch,
                     IssueLocation{.file = file.path.string()},
                     fmt::format("the {} has {} factor columns but {} rows; it must be square",
                                 what, size, document->num_rows()));
        return std::nullopt;
    }

    result.values = core::Matrix{size, size};
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            const auto value =
                document->field_as_double(row, column + first_column, report);
            if (!value.has_value()) {
                return std::nullopt;
            }
            result.values(row, column) = *value;
        }
    }

    return result;
}

/// @brief A coefficient CSV: rows are predictor or parameter names, columns are risk factors.
///
/// Keyed by [row name][lower-cased factor name], because the factor names in these files differ
/// in case from the correlation matrix's and from each other's.
using CoefficientTable = std::map<std::string, std::map<std::string, double>>;

std::optional<CoefficientTable> read_coefficient_csv(const FileBlock &file,
                                                     bool canonicalise_rows,
                                                     diag::IssueReport &report) {
    const auto document = io::read_csv(file.path, file.options, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    if (document->num_columns() < 2) {
        report.error(IssueCode::csv_missing_column, IssueLocation{.file = file.path.string()},
                     "a coefficient file needs a row-name column and at least one factor column");
        return std::nullopt;
    }

    CoefficientTable table;
    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto raw = document->field(row, 0);
        const auto name = canonicalise_rows ? canonical_predictor_row(raw) : raw;

        if (table.contains(name)) {
            report.error(IssueCode::csv_bad_value,
                         IssueLocation{.file = file.path.string(), .field = name,
                                       .line = document->line_of(row)},
                         fmt::format("'{}' appears twice; the later row would silently win",
                                     name));
            return std::nullopt;
        }

        auto &by_factor = table[name];
        for (std::size_t column = 1; column < document->num_columns(); ++column) {
            const auto value = document->field_as_double(row, column, report);
            if (!value.has_value()) {
                return std::nullopt;
            }
            by_factor[core::to_lower(document->headers()[column])] = *value;
        }
    }

    return table;
}

/// @brief A two-column `factor,coefficient` regression, as the income and physical-activity
///        models are written.
struct TwoColumnRegression {
    model::LinearModelParams model;
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::optional<double> stddev;
};

std::optional<TwoColumnRegression> read_two_column_regression(const FileBlock &file,
                                                             diag::IssueReport &report) {
    const auto document = io::read_csv(file.path, file.options, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    if (document->num_columns() != 2) {
        report.error(IssueCode::csv_missing_column, IssueLocation{.file = file.path.string()},
                     fmt::format("a two-column regression file must have exactly 2 columns, "
                                 "found {}",
                                 document->num_columns()));
        return std::nullopt;
    }

    TwoColumnRegression result;

    const auto take = [&](const std::string &name, double value) {
        if (core::case_insensitive::equals(name, "intercept")) {
            result.model.intercept = value;
        } else if (core::case_insensitive::equals(name, "min")) {
            result.min_value = value;
        } else if (core::case_insensitive::equals(name, "max")) {
            result.max_value = value;
        } else if (core::case_insensitive::equals(name, "stddev")) {
            result.stddev = value;
        } else {
            result.model.coefficients[core::Identifier{name}] = value;
        }
    };

    // These files have **no header row** — the first line is `Intercept,<value>` — but the reader
    // treats the first line as a header, as every other file here does have one. So the header is
    // put back as a data row unless it really is one, which is what a first cell spelling "Factor"
    // means. Getting this wrong silently loses the intercept, which for the physical-activity
    // model puts every person below the factor's lower bound and clamps them all to it.
    const auto &headers = document->headers();
    if (!core::case_insensitive::equals(headers[0], "Factor")) {
        try {
            take(headers[0], std::stod(headers[1]));
        } catch (const std::exception &) {
            report.error(IssueCode::csv_bad_value,
                         IssueLocation{.file = file.path.string(), .line = 1},
                         fmt::format("the first line is neither a 'Factor,Coefficient' header nor "
                                     "a name and a number: '{}', '{}'",
                                     headers[0], headers[1]));
            return std::nullopt;
        }
    }

    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto name = document->field(row, 0);
        const auto value = document->field_as_double(row, 1, report);
        if (!value.has_value()) {
            return std::nullopt;
        }
        take(name, *value);
    }

    if (result.model.coefficients.empty() && result.model.intercept == 0.0) {
        report.error(IssueCode::csv_bad_value, IssueLocation{.file = file.path.string()},
                     "the regression file has no intercept and no coefficients");
        return std::nullopt;
    }

    return result;
}

/// @brief Checks every predictor name of a model against the declared factors.
bool validate_model(const model::LinearModelParams &model, const std::filesystem::path &path,
                    const std::string &pointer, const LoadContext &context,
                    diag::IssueReport &report) {
    bool ok = true;
    for (const auto &[name, unused] : model.coefficients) {
        ok &= validate_predictor_name(name.to_string(), path, pointer, context, report);
    }
    for (const auto &[name, unused] : model.log_coefficients) {
        ok &= validate_predictor_name(name.to_string(), path, pointer, context, report);
    }
    return ok;
}

/// @brief Reorders `source`'s rows and columns into `target`'s factor order.
std::optional<core::Matrix> reorder_to(const MatrixCsv &source,
                                       const std::vector<core::Identifier> &target,
                                       const std::filesystem::path &path,
                                       diag::IssueReport &report) {
    std::vector<std::size_t> mapping;
    mapping.reserve(target.size());

    for (const auto &wanted : target) {
        bool found = false;
        for (std::size_t i = 0; i < source.names.size(); ++i) {
            if (core::case_insensitive::equals(source.names[i].to_string(),
                                               wanted.to_string())) {
                mapping.push_back(i);
                found = true;
                break;
            }
        }
        if (!found) {
            report.error(IssueCode::model_dimension_mismatch,
                         IssueLocation{.file = path.string(), .field = wanted.to_string()},
                         fmt::format("risk factor '{}' is in the correlation matrix but not in "
                                     "this file",
                                     wanted.to_string()));
            return std::nullopt;
        }
    }

    core::Matrix result{target.size(), target.size()};
    for (std::size_t row = 0; row < target.size(); ++row) {
        for (std::size_t column = 0; column < target.size(); ++column) {
            result(row, column) = source.values(mapping[row], mapping[column]);
        }
    }

    return result;
}

/// @brief The Cholesky factor, with a located diagnostic instead of an exception.
std::optional<core::Matrix> cholesky_of(const core::Matrix &matrix,
                                        const std::filesystem::path &path,
                                        const std::string &what, diag::IssueReport &report) {
    if (!matrix.is_finite()) {
        report.error(IssueCode::csv_bad_value, IssueLocation{.file = path.string()},
                     fmt::format("the {} contains a value that is not finite", what));
        return std::nullopt;
    }

    try {
        return matrix.cholesky_lower();
    } catch (const std::invalid_argument &error) {
        report.error(IssueCode::model_dimension_mismatch, IssueLocation{.file = path.string()},
                     fmt::format("the {} has no Cholesky factor: {}. A correlation or covariance "
                                 "matrix must be symmetric and positive definite",
                                 what, error.what()));
        return std::nullopt;
    }
}

double value_or(const std::map<std::string, double> &row, const std::string &key,
                double fallback) {
    const auto found = row.find(key);
    return found == row.end() ? fallback : found->second;
}

/// @brief Pulls one factor's column out of a coefficient table.
///
/// @param missing_is_error When true, a factor with no column in this table is a located error;
///        when false — the logistic table, where a factor without a first stage is the normal
///        case — it produces an empty model.
std::optional<model::LinearModelParams>
column_of(const CoefficientTable &table, const std::string &factor, bool missing_is_error,
          const std::filesystem::path &path, diag::IssueReport &report) {
    const auto intercept_row = table.find("Intercept");
    if (intercept_row == table.end()) {
        if (!missing_is_error) {
            return model::LinearModelParams{};
        }
        report.error(IssueCode::model_missing_key, IssueLocation{.file = path.string()},
                     "the coefficient file has no 'Intercept' row");
        return std::nullopt;
    }

    const auto intercept = intercept_row->second.find(factor);
    if (intercept == intercept_row->second.end()) {
        if (!missing_is_error) {
            return model::LinearModelParams{};
        }
        report.error(IssueCode::model_missing_key,
                     IssueLocation{.file = path.string(), .field = factor},
                     fmt::format("risk factor '{}' has no column in this coefficient file",
                                 factor));
        return std::nullopt;
    }

    model::LinearModelParams model;
    model.intercept = intercept->second;
    for (const auto &[row, by_factor] : table) {
        if (row == "Intercept" || is_parameter_row(row)) {
            continue;
        }
        const auto value = by_factor.find(factor);
        if (value != by_factor.end()) {
            model.coefficients[core::Identifier{row}] = value->second;
        }
    }

    return model;
}

/// @brief The per-stratum FactorsMean tables, loaded and checked against the factor set.
std::vector<model::IncomeStratumExpected>
load_income_strata(const LoadContext &context, const std::vector<core::Identifier> &names,
                   diag::IssueReport &report) {
    const auto &stratum_config =
        context.config->modelling.baseline_adjustments.income_stratum_factors_mean;

    std::vector<model::IncomeStratumExpected> strata;
    if (!stratum_config.enabled) {
        return strata;
    }

    io::CsvOptions options;
    const auto &delimiter = context.config->modelling.baseline_adjustments.delimiter;
    if (!delimiter.empty()) {
        options.delimiter = delimiter.front();
    }

    const auto max_age = static_cast<std::size_t>(context.config->settings.age_range.upper());

    for (const auto &stratum : stratum_config.strata) {
        auto table = std::make_shared<model::SexAgeFactorTable>();
        const std::vector<std::pair<core::Gender, std::filesystem::path>> files{
            {core::Gender::male, stratum.factorsmean_male},
            {core::Gender::female, stratum.factorsmean_female}};

        bool ok = true;
        for (const auto &[gender, path] : files) {
            const auto values = io::load_baseline_adjustments_from_csv(path, options, report);
            if (!values.has_value()) {
                ok = false;
                continue;
            }
            for (const auto &[factor, by_age] : *values) {
                if (by_age.size() <= max_age) {
                    report.error(IssueCode::csv_bad_value,
                                 IssueLocation{.file = path.string(),
                                               .field = factor.to_string()},
                                 fmt::format("income stratum '{}': the FactorsMean table covers "
                                             "{} ages, but the configured age range needs {}",
                                             stratum.id, by_age.size(), max_age + 1));
                    ok = false;
                    continue;
                }
                table->emplace(gender, factor, by_age);
            }
        }

        // Every factor the model generates has to be in every stratum's table, or a stratum's
        // calibration would silently skip a factor the overall pass calibrates.
        for (const auto &factor : names) {
            for (const auto gender : {core::Gender::male, core::Gender::female}) {
                if (!table->contains(gender, factor)) {
                    report.error(IssueCode::model_missing_key,
                                 IssueLocation{.file = stratum.factorsmean_male.string(),
                                               .field = factor.to_string()},
                                 fmt::format("income stratum '{}' has no {} column for risk "
                                             "factor '{}'",
                                             stratum.id,
                                             gender == core::Gender::male ? "male" : "female",
                                             factor.to_string()));
                    ok = false;
                }
            }
        }

        if (ok) {
            strata.push_back(model::IncomeStratumExpected{.id = stratum.id,
                                                          .expected = std::move(table)});
        }
    }

    return strata;
}

} // namespace

std::unique_ptr<model::RiskFactorModel> load_static_linear(const nlohmann::json &document,
                                                            const std::filesystem::path &path,
                                                            const LoadContext &context,
                                                            diag::IssueReport &report) {
    const auto before = report.error_count();
    const io::JsonCursor root{document, path.string(), "", report};
    const auto root_path = path.parent_path();
    const auto &requirements = context.config->project_requirements;

    root.reject_unknown_members({"$schema", "$comment", "ModelName", "InformationSpeed",
                                 "PhysicalActivityStdDev", "RuralPrevalence", "RegionFile",
                                 "EthnicityFile", "IncomeModels", "PhysicalActivityModels",
                                 "Nutrients", "RiskFactorModels", "RiskFactorCorrelationFile",
                                 "PolicyCovarianceFile"});

    auto parameters = std::make_shared<model::StaticLinearParameters>();

    // The expected-value trend the calibration applies, built from this model file rather than
    // shared: which table it is depends on the trend type, and only this family has one.
    auto expected_trend = std::make_shared<std::map<core::Identifier, double>>();
    auto expected_trend_steps = std::make_shared<std::map<core::Identifier, int>>();
    auto expected_decay = std::make_shared<std::map<core::Identifier, double>>();

    // --- the factor order, which everything else is indexed by -----------------------------
    const auto correlation_file = read_file_block(root, "RiskFactorCorrelationFile", root_path);
    if (!correlation_file.has_value()) {
        return nullptr;
    }
    const auto correlation =
        read_matrix_csv(*correlation_file, true, "risk factor correlation matrix", report);
    if (!correlation.has_value()) {
        return nullptr;
    }
    parameters->names = correlation->names;

    const auto cholesky = cholesky_of(correlation->values, correlation_file->path,
                                       "risk factor correlation matrix", report);
    if (!cholesky.has_value()) {
        return nullptr;
    }
    parameters->cholesky = *cholesky;

    const auto policy_file = read_file_block(root, "PolicyCovarianceFile", root_path);
    if (!policy_file.has_value()) {
        return nullptr;
    }
    const auto policy_covariance =
        read_matrix_csv(*policy_file, false, "intervention policy covariance matrix", report);
    if (!policy_covariance.has_value()) {
        return nullptr;
    }

    const auto reordered = reorder_to(*policy_covariance, parameters->names, policy_file->path,
                                       report);
    if (!reordered.has_value()) {
        return nullptr;
    }
    const auto policy_cholesky = cholesky_of(*reordered, policy_file->path,
                                              "intervention policy covariance matrix", report);
    if (!policy_cholesky.has_value()) {
        return nullptr;
    }
    parameters->policy_cholesky = *policy_cholesky;

    // --- the per-factor models, in whichever of the two shapes the file uses -----------------
    const auto risk_factor_models = root.object("RiskFactorModels");
    if (!risk_factor_models.has_value()) {
        return nullptr;
    }

    const bool matrix_based = risk_factor_models->has("boxcox_coefficients");

    CoefficientTable boxcox;
    CoefficientTable policy_coefficients;
    CoefficientTable logistic;

    if (matrix_based) {
        const auto boxcox_file =
            read_file_block(*risk_factor_models, "boxcox_coefficients", root_path);
        if (!boxcox_file.has_value()) {
            return nullptr;
        }
        const auto table = read_coefficient_csv(*boxcox_file, false, report);
        if (!table.has_value()) {
            return nullptr;
        }
        boxcox = *table;

        if (risk_factor_models->has("policy_coefficients")) {
            const auto file =
                read_file_block(*risk_factor_models, "policy_coefficients", root_path);
            if (!file.has_value()) {
                return nullptr;
            }
            const auto values = read_coefficient_csv(*file, true, report);
            if (!values.has_value()) {
                return nullptr;
            }
            policy_coefficients = *values;
        }

        // The first stage is used when the model file provides it, and a factor absent from that
        // file simply has no first stage. `project_requirements.two_stage.use_logistic` does not
        // decide it: the baseline ignores that flag entirely and reads the file whenever it is
        // there, and the upstream FINCH pack relies on exactly that — its config says false while
        // its model carries a logistic regression for seven food groups, and the numbers the pack
        // was fitted to are the ones with the first stage on.
        //
        // Following the file rather than the flag is therefore the behaviour, but the
        // disagreement is said out loud rather than passed over in silence as it is upstream.
        // docs/deviations.md.
        if (risk_factor_models->has("logistic_regression")) {
            if (!requirements.two_stage.use_logistic) {
                risk_factor_models->warning(
                    "logistic_regression", IssueCode::config_default_applied,
                    "this model carries a logistic regression, so the two-stage first step is "
                    "used, even though project_requirements.two_stage.use_logistic is false; the "
                    "model file decides, as it does upstream. Remove the file to turn the first "
                    "step off");
            }
            const auto file =
                read_file_block(*risk_factor_models, "logistic_regression", root_path);
            if (!file.has_value()) {
                return nullptr;
            }
            const auto values = read_coefficient_csv(*file, false, report);
            if (!values.has_value()) {
                return nullptr;
            }
            logistic = *values;
        } else if (requirements.two_stage.use_logistic) {
            risk_factor_models->error(
                "logistic_regression", IssueCode::model_missing_key,
                "project_requirements.two_stage.use_logistic is true but this model has no "
                "'logistic_regression' file, so there is nothing to do the first step with");
            return nullptr;
        }
    }

    for (const auto &factor : parameters->names) {
        const auto lower = core::to_lower(factor.to_string());
        const auto pointer = fmt::format("/RiskFactorModels/{}", factor.to_string());

        if (matrix_based) {
            auto model = column_of(boxcox, lower, true, correlation_file->path, report);
            if (!model.has_value()) {
                return nullptr;
            }
            validate_model(*model, path, pointer, context, report);
            parameters->models.push_back(std::move(*model));

            const auto parameter = [&](const std::string &row) -> std::optional<double> {
                const auto found = boxcox.find(row);
                if (found == boxcox.end() || !found->second.contains(lower)) {
                    report.error(IssueCode::model_missing_key,
                                 IssueLocation{.file = path.string(), .field = pointer},
                                 fmt::format("the Box-Cox file has no '{}' for risk factor '{}'",
                                             row, factor.to_string()));
                    return std::nullopt;
                }
                return found->second.at(lower);
            };

            const auto lambda = parameter("lambda");
            const auto stddev = parameter("stddev");
            const auto minimum = parameter("min");
            const auto maximum = parameter("max");
            if (!lambda || !stddev || !minimum || !maximum) {
                return nullptr;
            }
            parameters->lambda.push_back(*lambda);
            parameters->stddev.push_back(*stddev);
            parameters->ranges.emplace_back(*minimum, *maximum);

            auto policy_model =
                column_of(policy_coefficients, lower, !policy_coefficients.empty(),
                          policy_file->path, report);
            if (!policy_model.has_value()) {
                return nullptr;
            }
            validate_model(*policy_model, path, pointer, context, report);
            parameters->policy_models.push_back(std::move(*policy_model));
            parameters->policy_ranges.emplace_back(
                value_or(policy_coefficients.contains("min") ? policy_coefficients.at("min")
                                                              : std::map<std::string, double>{},
                         lower, 0.0),
                value_or(policy_coefficients.contains("max") ? policy_coefficients.at("max")
                                                              : std::map<std::string, double>{},
                         lower, 0.0));

            auto logistic_model = column_of(logistic, lower, false, path, report);
            if (!logistic_model.has_value()) {
                return nullptr;
            }
            validate_model(*logistic_model, path, pointer, context, report);
            parameters->logistic_models.push_back(std::move(*logistic_model));

            // The matrix shape carries no trend equations. A trend-enabled config on this shape
            // is refused below rather than being given empty ones.
            parameters->trend_models.emplace_back();
            parameters->trend_ranges.emplace_back(0.0, 1.0);
            parameters->trend_lambda.push_back(1.0);
            parameters->expected_trend_boxcox[factor] = 1.0;
            continue;
        }

        // The JSON shape: one object per factor, keyed case-insensitively because the packs and
        // the correlation matrix disagree about capitalisation.
        std::string key;
        for (const auto &member : risk_factor_models->node().items()) {
            if (core::case_insensitive::equals(member.key(), factor.to_string())) {
                key = member.key();
                break;
            }
        }
        if (key.empty()) {
            report.error(IssueCode::model_missing_key,
                         IssueLocation{.file = path.string(), .field = pointer},
                         fmt::format("risk factor '{}' is in the correlation matrix but has no "
                                     "entry in RiskFactorModels",
                                     factor.to_string()));
            return nullptr;
        }

        const io::JsonCursor entry{risk_factor_models->node().at(key), path.string(),
                                   fmt::format("/RiskFactorModels/{}", key), report};
        entry.reject_unknown_members({"Intercept", "Coefficients", "LogCoefficients", "Range",
                                      "Lambda", "StdDev", "Policy", "Trend", "ExpectedTrend",
                                      "ExpectedTrendBoxCox", "TrendSteps", "IncomeTrend",
                                      "ExpectedIncomeTrend", "ExpectedIncomeTrendBoxCox",
                                      "IncomeTrendSteps", "IncomeDecayFactor"});

        const auto read_linear = [&](const io::JsonCursor &cursor,
                                      const std::string &where) -> model::LinearModelParams {
            model::LinearModelParams model;
            model.intercept = cursor.number("Intercept").value_or(0.0);
            if (const auto coefficients = cursor.optional_object("Coefficients")) {
                for (const auto &member : coefficients->node().items()) {
                    if (!member.value().is_number()) {
                        coefficients->error(member.key(), IssueCode::config_bad_value,
                                            "a coefficient must be a number");
                        continue;
                    }
                    model.coefficients[core::Identifier{member.key()}] =
                        member.value().get<double>();
                }
            }
            if (const auto logs = cursor.optional_object("LogCoefficients")) {
                for (const auto &member : logs->node().items()) {
                    if (!member.value().is_number()) {
                        logs->error(member.key(), IssueCode::config_bad_value,
                                    "a coefficient must be a number");
                        continue;
                    }
                    model.log_coefficients[core::Identifier{member.key()}] =
                        member.value().get<double>();
                }
            }
            validate_model(model, path, where, context, report);
            return model;
        };

        parameters->models.push_back(read_linear(entry, entry.pointer()));

        const auto range = entry.number_array("Range");
        const auto lambda = entry.number("Lambda");
        const auto stddev = entry.number("StdDev");
        if (!range || range->size() != 2 || !lambda || !stddev) {
            entry.error("Range", IssueCode::model_missing_key,
                        "a risk factor model needs Range [min, max], Lambda and StdDev");
            return nullptr;
        }
        parameters->ranges.emplace_back((*range)[0], (*range)[1]);
        parameters->lambda.push_back(*lambda);
        parameters->stddev.push_back(*stddev);

        const auto policy = entry.object("Policy");
        if (!policy.has_value()) {
            return nullptr;
        }
        policy->reject_unknown_members({"Intercept", "Coefficients", "LogCoefficients", "Range"});
        parameters->policy_models.push_back(read_linear(*policy, policy->pointer()));
        const auto policy_range = policy->number_array("Range");
        if (!policy_range || policy_range->size() != 2) {
            policy->error("Range", IssueCode::model_missing_key,
                          "an intervention policy needs Range [min, max]");
            return nullptr;
        }
        parameters->policy_ranges.emplace_back((*policy_range)[0], (*policy_range)[1]);

        // The JSON shape has no first stage: it predates two-stage modelling.
        parameters->logistic_models.emplace_back();

        if (const auto trend = entry.optional_object("Trend")) {
            trend->reject_unknown_members(
                {"Intercept", "Coefficients", "LogCoefficients", "Range", "Lambda"});
            parameters->trend_models.push_back(read_linear(*trend, trend->pointer()));
            const auto trend_range = trend->number_array("Range");
            parameters->trend_ranges.emplace_back(
                trend_range && trend_range->size() == 2 ? (*trend_range)[0] : 0.0,
                trend_range && trend_range->size() == 2 ? (*trend_range)[1] : 1.0);
            parameters->trend_lambda.push_back(trend->number("Lambda").value_or(1.0));
        } else {
            parameters->trend_models.emplace_back();
            parameters->trend_ranges.emplace_back(0.0, 1.0);
            parameters->trend_lambda.push_back(1.0);
        }
        parameters->expected_trend_boxcox[factor] =
            entry.node().value("ExpectedTrendBoxCox", 1.0);
        (*expected_trend)[factor] = entry.node().value("ExpectedTrend", 1.0);
        (*expected_trend_steps)[factor] = entry.node().value("TrendSteps", 0);

        // The income trend: a second set of equations that scale a factor by
        // `trend * exp(decay * years)` from the second simulated year. Only the India packs carry
        // them, and only when project_requirements.trend.type is income_trend.
        if (const auto income_trend = entry.optional_object("IncomeTrend")) {
            income_trend->reject_unknown_members(
                {"Intercept", "Coefficients", "LogCoefficients", "Range", "Lambda"});
            parameters->income_trend_models.push_back(
                read_linear(*income_trend, income_trend->pointer()));

            const auto trend_range = income_trend->number_array("Range");
            parameters->income_trend_ranges.emplace_back(
                trend_range && trend_range->size() == 2 ? (*trend_range)[0] : 0.0,
                trend_range && trend_range->size() == 2 ? (*trend_range)[1] : 1.0);
            parameters->income_trend_lambda.push_back(income_trend->number("Lambda").value_or(1.0));

            parameters->expected_income_trend_boxcox[factor] =
                entry.node().value("ExpectedIncomeTrendBoxCox", 1.0);
            parameters->income_trend_steps[factor] = entry.node().value("IncomeTrendSteps", 0);
            parameters->income_trend_decay_factors[factor] =
                entry.node().value("IncomeDecayFactor", 0.0);

            (*expected_decay)[factor] = entry.node().value("IncomeDecayFactor", 0.0);
        }
    }

    // --- the rest of the model ---------------------------------------------------------------
    parameters->info_speed = root.number("InformationSpeed").value_or(0.0);

    for (const auto &group : root.array("RuralPrevalence")) {
        group.reject_unknown_members({"Name", "Female", "Male"});
        const auto name = group.string("Name");
        const auto female = group.number("Female");
        const auto male = group.number("Male");
        if (!name || !female || !male) {
            continue;
        }
        parameters->rural_prevalence[core::Identifier{*name}] =
            model::GenderValue<double>{.male = *male, .female = *female};
    }

    // --- income --------------------------------------------------------------------------
    parameters->income_enabled = requirements.income.enabled;
    try {
        parameters->income_layout =
            core::income_category_layout_from_config(requirements.income.categories);
    } catch (const std::invalid_argument &error) {
        report.error(IssueCode::config_bad_value,
                     IssueLocation{.field = "/project_requirements/income/categories"},
                     error.what());
        return nullptr;
    }

    parameters->continuous_income = requirements.income.type == "continuous";

    // Checked here, before anything is read, because it depends only on two config values and
    // because reporting it after the income model has failed for its own reasons would bury it.
    if (context.config->modelling.baseline_adjustments.income_stratum_factors_mean.enabled &&
        !parameters->continuous_income) {
        report.error(IssueCode::config_bad_value,
                     IssueLocation{.field = "/modelling/baseline_adjustments/"
                                            "income_stratum_factors_mean/enabled"},
                     "per-stratum FactorsMean adjustment needs a continuous income model, "
                     "because the strata are ranks of a continuous income");
    }

    const auto income_models = root.object("IncomeModels");
    if (!income_models.has_value()) {
        return nullptr;
    }

    if (parameters->continuous_income) {
        if (!income_models->has("continuous")) {
            income_models->error(
                "continuous", IssueCode::model_missing_key,
                "project_requirements.income.type is \"continuous\" but this model has no "
                "IncomeModels.continuous entry; add one or set income.type to \"categorical\"");
            return nullptr;
        }
        const auto file = read_file_block(*income_models, "continuous", root_path, "csv_file");
        if (!file.has_value()) {
            return nullptr;
        }
        const auto regression = read_two_column_regression(*file, report);
        if (!regression.has_value()) {
            return nullptr;
        }
        parameters->continuous_income_model = regression->model;
        validate_model(parameters->continuous_income_model, file->path, "", context, report);
        if (regression->min_value) {
            parameters->continuous_income_model.coefficients[core::Identifier{"min"}] =
                *regression->min_value;
        }
        if (regression->max_value) {
            parameters->continuous_income_model.coefficients[core::Identifier{"max"}] =
                *regression->max_value;
        }
        if (regression->stddev) {
            parameters->continuous_income_model.coefficients[core::Identifier{"stddev"}] =
                *regression->stddev;
        }
    } else {
        std::size_t categorical = 0;
        for (const auto &member : income_models->node().items()) {
            if (member.key() == "continuous" || member.key() == "simple") {
                continue;
            }

            const auto lower = core::to_lower(member.key());
            std::optional<core::Income> category;
            for (const auto candidate : parameters->income_layout.strata) {
                if (core::case_insensitive::equals(core::income_name(candidate), lower)) {
                    category = candidate;
                    break;
                }
            }
            if (!category.has_value()) {
                income_models->error(
                    member.key(), IssueCode::config_bad_value,
                    fmt::format("'{}' is not one of the {} income categories this project "
                                "declares",
                                member.key(), parameters->income_layout.count));
                continue;
            }

            const io::JsonCursor entry{member.value(), path.string(),
                                       fmt::format("/IncomeModels/{}", member.key()), report};
            entry.reject_unknown_members({"Intercept", "Coefficients"});

            model::LinearModelParams model;
            model.intercept = entry.number("Intercept").value_or(0.0);
            if (const auto coefficients = entry.optional_object("Coefficients")) {
                for (const auto &coefficient : coefficients->node().items()) {
                    if (!coefficient.value().is_number()) {
                        continue;
                    }
                    model.coefficients[core::Identifier{coefficient.key()}] =
                        coefficient.value().get<double>();
                }
            }
            validate_model(model, path, entry.pointer(), context, report);
            parameters->income_models[*category] = std::move(model);
            ++categorical;
        }

        if (parameters->income_enabled && categorical != parameters->income_layout.count) {
            income_models->error(
                "", IssueCode::model_dimension_mismatch,
                fmt::format("project_requirements.income.categories is \"{}\" but this model "
                            "defines {} categorical income model(s); it needs {}",
                            requirements.income.categories, categorical,
                            parameters->income_layout.count));
            return nullptr;
        }
    }

    // --- physical activity ------------------------------------------------------------------
    parameters->physical_activity_enabled = requirements.physical_activity.enabled;
    if (parameters->physical_activity_enabled) {
        const auto &type = requirements.physical_activity.type;
        parameters->physical_activity.type = type;

        const auto models = root.optional_object("PhysicalActivityModels");

        // Some packs carry a top-level PhysicalActivityStdDev of `null`, meaning "the model
        // block has it". Read as an optional rather than as a required number, so a null there is
        // not an error in a config that never needed it.
        const auto root_stddev =
            root.node().contains("PhysicalActivityStdDev") &&
                    root.node()["PhysicalActivityStdDev"].is_number()
                ? root.node()["PhysicalActivityStdDev"].get<double>()
                : 0.0;

        // The block is **optional**, and the type check applies only when it is there. That is the
        // baseline's own rule — `pa_req.enabled && opt.contains("PhysicalActivityModels")`,
        // model_parser.cpp:1716, falling through to the root standard deviation otherwise — and the
        // published PIF pack is the model that needs it: `KevinHall_PIF/static_model.json` declares
        // `physical_activity.type: "simple"`, carries a root `PhysicalActivityStdDev` and has no
        // `PhysicalActivityModels` block at all. Requiring the block made that example unloadable for
        // a wrapper whose only contents would have been the number already present.
        //
        // A "simple" model needs nothing but a standard deviation, so there is nothing for the wrapper
        // to add. A "continuous" one needs a fitted regression, so for that one the block is required
        // and its absence is the error below.
        if (!models.has_value()) {
            if (type == "continuous") {
                report.error(IssueCode::model_missing_key,
                             IssueLocation{.file = path.string(),
                                           .field = "/PhysicalActivityModels/continuous"},
                             "project_requirements.physical_activity.type is \"continuous\", which "
                             "needs a fitted regression, but this model has no "
                             "PhysicalActivityModels block");
                return nullptr;
            }
            parameters->physical_activity.stddev = root_stddev;
        } else if (!models->has(type)) {
            report.error(IssueCode::model_missing_key,
                         IssueLocation{.file = path.string(),
                                       .field = fmt::format("/PhysicalActivityModels/{}", type)},
                         fmt::format("project_requirements.physical_activity.type is \"{}\" and this "
                                     "model has a PhysicalActivityModels block, but no {} entry in "
                                     "it",
                                     type, type));
            return nullptr;
        } else if (type == "continuous") {
            const auto file = read_file_block(*models, type, root_path, "csv_file");
            if (!file.has_value()) {
                return nullptr;
            }
            const auto regression = read_two_column_regression(*file, report);
            if (!regression.has_value()) {
                return nullptr;
            }
            parameters->physical_activity.linear = regression->model;
            validate_model(parameters->physical_activity.linear, file->path, "", context, report);
            parameters->physical_activity.min_value = regression->min_value;
            parameters->physical_activity.max_value = regression->max_value;
            parameters->physical_activity.stddev = regression->stddev.value_or(0.0);
        } else {
            const io::JsonCursor entry{models->node().at(type), path.string(),
                                       fmt::format("/PhysicalActivityModels/{}", type), report};
            entry.reject_unknown_members({"PhysicalActivityStdDev"});
            parameters->physical_activity.stddev =
                entry.node().contains("PhysicalActivityStdDev") &&
                        entry.node()["PhysicalActivityStdDev"].is_number()
                    ? entry.node()["PhysicalActivityStdDev"].get<double>()
                    : root_stddev;
        }

        if (parameters->physical_activity.stddev <= 0.0) {
            report.error(IssueCode::config_bad_value,
                         IssueLocation{.file = path.string(),
                                       .field = "/PhysicalActivityModels"},
                         "the physical activity model has no positive standard deviation; with "
                         "zero it would give every person the same value");
            return nullptr;
        }
    }

    // --- the income strata and the requirement flags -----------------------------------------
    const auto &stratum_config =
        context.config->modelling.baseline_adjustments.income_stratum_factors_mean;
    parameters->income_stratum_adjustment_enabled = stratum_config.enabled;
    parameters->adjustment_income_stratum_count = stratum_config.adjustment_income_stratum_count;
    parameters->income_stratum_expected = load_income_strata(context, parameters->names, report);

    parameters->gender2_indicator =
        model::parse_gender2_indicator(requirements.demographics.gender2);
    parameters->max_age_for_linear_models = requirements.demographics.max_age_for_linear_models;
    parameters->adjust_factors_to_mean = requirements.risk_factors.adjust_to_factors_mean;
    parameters->factors_trended = requirements.risk_factors.trended;
    parameters->adjust_income_to_mean = requirements.income.adjust_to_factors_mean;
    parameters->income_trended = requirements.income.trended;
    parameters->adjust_physical_activity_to_mean =
        requirements.physical_activity.adjust_to_factors_mean;
    parameters->physical_activity_trended = requirements.physical_activity.trended;
    parameters->trend_enabled = requirements.trend.enabled;

    if (requirements.trend.enabled) {
        const auto &type = requirements.trend.type;
        if (type == "trend" || type == "upf_trend") {
            parameters->trend_type = model::TrendType::UpfTrend;
            if (matrix_based) {
                report.error(IssueCode::feature_not_implemented,
                             IssueLocation{.field = "/project_requirements/trend/type"},
                             "the UPF trend needs per-factor Trend equations, which the CSV "
                             "matrix form of this model does not carry; the upstream packs put "
                             "them in the JSON form only");
            }
        } else if (type == "income_trend") {
            parameters->trend_type = model::TrendType::IncomeTrend;
            if (matrix_based) {
                report.error(IssueCode::feature_not_implemented,
                             IssueLocation{.field = "/project_requirements/trend/type"},
                             "the income trend needs per-factor IncomeTrend equations, which the "
                             "CSV matrix form of this model does not carry; the upstream packs "
                             "put them in the JSON form only");
            }
        } else if (type != "null") {
            report.error(IssueCode::config_bad_value,
                         IssueLocation{.field = "/project_requirements/trend/type"},
                         fmt::format("unknown trend type '{}'", type));
        }

        // A trend that is switched on needs one equation per factor, or a factor would silently go
        // untrended while the rest of the cohort moved.
        const auto needed = parameters->names.size();
        const auto have = parameters->trend_type == model::TrendType::IncomeTrend
                              ? parameters->income_trend_models.size()
                              : parameters->trend_models.size();
        if (parameters->trend_type != model::TrendType::Null && have != needed) {
            report.error(IssueCode::model_missing_key,
                         IssueLocation{.file = path.string()},
                         fmt::format("project_requirements.trend.type is '{}' but this model has "
                                     "{} trend equation(s) for {} risk factors",
                                     type, have, needed));
        }
    }

    // Skipping the whole policy path when every coefficient is zero changes the random stream, so
    // it is a property of the model rather than an optimisation, and it is stated in the report.
    parameters->has_active_policies = false;
    for (const auto &model : parameters->policy_models) {
        if (model.intercept != 0.0) {
            parameters->has_active_policies = true;
            break;
        }
        for (const auto &[unused, value] : model.coefficients) {
            if (value != 0.0) {
                parameters->has_active_policies = true;
                break;
            }
        }
        for (const auto &[unused, value] : model.log_coefficients) {
            if (value != 0.0) {
                parameters->has_active_policies = true;
                break;
            }
        }
        if (parameters->has_active_policies) {
            break;
        }
    }

    // Every factor the model generates must be in the overall FactorsMean tables, or its
    // calibration would silently do nothing.
    for (const auto &factor : parameters->names) {
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            if (!context.expected->contains(gender, factor)) {
                report.error(IssueCode::model_missing_key,
                             IssueLocation{.file = path.string(), .field = factor.to_string()},
                             fmt::format("risk factor '{}' has no {} column in the FactorsMean "
                                         "tables, so it could never be calibrated",
                                         factor.to_string(),
                                         gender == core::Gender::male ? "male" : "female"));
            }
        }
    }

    if (report.error_count() != before) {
        return nullptr;
    }

    // Which table the calibration's trend reads depends on the trend type, so it is chosen here
    // rather than by the model.
    std::shared_ptr<const std::map<core::Identifier, double>> calibration_trend;
    std::shared_ptr<const std::map<core::Identifier, double>> calibration_decay;
    switch (parameters->trend_type) {
    case model::TrendType::Null:
        break;
    case model::TrendType::UpfTrend:
        calibration_trend = expected_trend;
        break;
    case model::TrendType::IncomeTrend: {
        auto income_trend = std::make_shared<std::map<core::Identifier, double>>();
        for (const auto &member : risk_factor_models->node().items()) {
            if (member.value().is_object() && member.value().contains("ExpectedIncomeTrend")) {
                (*income_trend)[core::Identifier{member.key()}] =
                    member.value()["ExpectedIncomeTrend"].get<double>();
            }
        }
        calibration_trend = income_trend;
        calibration_decay = expected_decay;
        break;
    }
    }

    return std::make_unique<model::StaticLinearModel>(context.expected, calibration_trend,
                                                      expected_trend_steps,
                                                      std::move(parameters), calibration_decay);
}

std::optional<RegionEthnicityPrevalence>
load_region_and_ethnicity(const nlohmann::json &document, const std::filesystem::path &path,
                          const LoadContext &context, diag::IssueReport &report) {
    const auto before = report.error_count();
    const io::JsonCursor root{document, path.string(), "", report};
    const auto root_path = path.parent_path();
    const auto &demographics = context.config->project_requirements.demographics;

    RegionEthnicityPrevalence result;

    if (demographics.region) {
        const auto file = read_file_block(root, "RegionFile", root_path);
        if (!file.has_value()) {
            report.error(IssueCode::model_missing_key,
                         IssueLocation{.file = path.string(), .field = "/RegionFile"},
                         "project_requirements.demographics.region is true but this model has no "
                         "RegionFile; without it nobody could be given a region");
            return std::nullopt;
        }

        const auto csv = io::read_csv(file->path, file->options, report);
        if (!csv.has_value()) {
            return std::nullopt;
        }

        const auto age_column = csv->column_index("Age");
        const auto gender_column = csv->column_index("Gender");
        if (!age_column || !gender_column) {
            report.error(IssueCode::csv_missing_column, IssueLocation{.file = file->path.string()},
                         "the region file needs 'Age' and 'Gender' columns");
            return std::nullopt;
        }

        std::vector<std::pair<std::size_t, std::string>> regions;
        for (std::size_t column = 0; column < csv->num_columns(); ++column) {
            if (core::to_lower(csv->headers()[column]).starts_with("region")) {
                regions.emplace_back(column, csv->headers()[column]);
            }
        }
        if (regions.empty()) {
            report.error(IssueCode::csv_missing_column, IssueLocation{.file = file->path.string()},
                         "the region file has no 'region…' columns");
            return std::nullopt;
        }

        for (std::size_t row = 0; row < csv->num_rows(); ++row) {
            const auto age = csv->field_as_int(row, *age_column, report);
            const auto gender_value = csv->field_as_int(row, *gender_column, report);
            if (!age || !gender_value) {
                continue;
            }
            const auto gender =
                *gender_value == 1 ? core::Gender::male : core::Gender::female;
            const core::Identifier key{fmt::format("age_{}", *age)};

            for (const auto &[column, name] : regions) {
                const auto share = csv->field_as_double(row, column, report);
                if (share.has_value()) {
                    result.region[key][gender][name] = *share;
                }
            }
        }
    }

    if (demographics.ethnicity) {
        const auto file = read_file_block(root, "EthnicityFile", root_path);
        if (!file.has_value()) {
            report.error(IssueCode::model_missing_key,
                         IssueLocation{.file = path.string(), .field = "/EthnicityFile"},
                         "project_requirements.demographics.ethnicity is true but this model has "
                         "no EthnicityFile; without it nobody could be given an ethnicity");
            return std::nullopt;
        }

        const auto csv = io::read_csv(file->path, file->options, report);
        if (!csv.has_value()) {
            return std::nullopt;
        }

        const auto adult_column = csv->column_index("adult");
        const auto gender_column = csv->column_index("gender");
        const auto ethnicity_column = csv->column_index("ethnicity");
        if (!adult_column || !gender_column || !ethnicity_column) {
            report.error(IssueCode::csv_missing_column, IssueLocation{.file = file->path.string()},
                         "the ethnicity file needs 'adult', 'gender' and 'ethnicity' columns");
            return std::nullopt;
        }

        std::vector<std::pair<std::size_t, std::string>> regions;
        for (std::size_t column = 0; column < csv->num_columns(); ++column) {
            if (core::to_lower(csv->headers()[column]).starts_with("region")) {
                regions.emplace_back(column, csv->headers()[column]);
            }
        }
        if (regions.empty()) {
            report.error(IssueCode::csv_missing_column, IssueLocation{.file = file->path.string()},
                         "the ethnicity file has no 'region…' columns");
            return std::nullopt;
        }

        for (std::size_t row = 0; row < csv->num_rows(); ++row) {
            const auto adult = csv->field_as_int(row, *adult_column, report);
            const auto gender_value = csv->field_as_int(row, *gender_column, report);
            const auto ethnicity = csv->field_as_int(row, *ethnicity_column, report);
            if (!adult || !gender_value || !ethnicity) {
                continue;
            }
            const core::Identifier group{*adult == 0 ? "under18" : "over18"};
            const auto gender =
                *gender_value == 1 ? core::Gender::male : core::Gender::female;

            for (const auto &[column, name] : regions) {
                const auto share = csv->field_as_double(row, column, report);
                if (share.has_value()) {
                    result.ethnicity[group][gender][name][std::to_string(*ethnicity)] = *share;
                }
            }
        }
    }

    if (report.error_count() != before) {
        return std::nullopt;
    }

    return result;
}

} // namespace hgps::config::models::detail
