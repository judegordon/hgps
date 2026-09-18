#include "loader.h"

#include "core/string_util.h"
#include "io/paths.h"
#include "io/sha256.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <set>
#include <utility>

#include <fmt/format.h>

namespace hgps::config {
namespace {

using diag::IssueCode;
using diag::IssueLocation;
using io::JsonCursor;

/// The `$schema` value config v2 documents carry. Checked for shape, never dereferenced: nothing
/// is fetched from the network at load time.
constexpr const char *kSchemaUrlFragment = "schemas/v2/config.json";

/// The UTC wall-clock time as YYYY-MM-DD_HH-MM-SS, for the optional {TIMESTAMP} token.
std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
    ::gmtime_r(&now, &parts);

    std::array<char, 32> buffer{};
    const auto written = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d_%H-%M-%S", &parts);
    return std::string{buffer.data(), written};
}

std::string expand_folder(const std::string &folder, const IssueLocation &where,
                          diag::IssueReport &report) {
    std::vector<std::string> undefined;
    auto expanded = io::expand_environment_variables(folder, undefined);

    for (const auto &name : undefined) {
        // The baseline expands an undefined variable to the empty string, which silently
        // relocates a run's output (audit N-17).
        report.error(IssueCode::config_undefined_variable, where,
                     fmt::format("environment variable '{}' is not set", name));
    }

    return expanded;
}

} // namespace

namespace detail {

std::optional<std::filesystem::path> resolve_path(const JsonCursor &cursor,
                                                  const std::string &field,
                                                  const std::filesystem::path &root_path,
                                                  bool require_exists,
                                                  diag::IssueReport &report) {
    const auto value = cursor.string(field);
    if (!value.has_value()) {
        return std::nullopt;
    }

    std::vector<std::string> undefined;
    const auto expanded = io::expand_environment_variables(*value, undefined);
    for (const auto &name : undefined) {
        report.error(IssueCode::config_undefined_variable, cursor.location_of(field),
                     fmt::format("environment variable '{}' is not set", name));
    }
    if (!undefined.empty()) {
        return std::nullopt;
    }

    std::filesystem::path path{expanded};
    if (path.is_relative()) {
        path = std::filesystem::absolute(root_path / path);
    }

    if (require_exists && !std::filesystem::exists(path)) {
        report.error(IssueCode::file_not_found, cursor.location_of(field),
                     fmt::format("'{}' does not exist (resolved to {})", *value, path.string()));
        return std::nullopt;
    }

    return path;
}

bool check_version(const JsonCursor &root, diag::IssueReport &report) {
    const auto version = root.integer("version");
    if (!version.has_value()) {
        return false;
    }

    if (*version != kConfigVersion) {
        report.error(IssueCode::config_bad_value, root.location_of("version"),
                     fmt::format("this build reads config version {}, the file says {}; convert it "
                                 "with tools/convert-config",
                                 kConfigVersion, *version));
        return false;
    }

    // The $schema is what actually identifies the format, because an upstream v1 config also says
    // version 2. A wrong one is a warning: the document has already been validated by the loader,
    // which is the authority (ADR 0022).
    if (const auto schema = root.node().find("$schema"); schema != root.node().end()) {
        if (!schema->is_string()) {
            report.error(IssueCode::config_wrong_type, root.location_of("$schema"),
                         "expected a string");
            return false;
        }
        const auto text = schema->get<std::string>();
        if (text.find(kSchemaUrlFragment) == std::string::npos) {
            report.warning(IssueCode::config_schema_mismatch, root.location_of("$schema"),
                           fmt::format("'{}' does not point at this repository's {}; the file may "
                                       "be an upstream v1 config, which tools/convert-config "
                                       "converts",
                                       text, kSchemaUrlFragment));
        }
    } else {
        report.warning(IssueCode::config_default_applied, root.location_of("$schema"),
                       fmt::format("no '$schema'; assuming this repository's {}",
                                   kSchemaUrlFragment));
    }

    return true;
}

std::optional<ProjectRequirements> load_project_requirements(const JsonCursor &root,
                                                             diag::IssueReport &report) {
    const auto before = report.error_count();

    const auto cursor = root.object("project_requirements");
    if (!cursor.has_value()) {
        // Required in config v2, unlike upstream: the behaviour it gates is otherwise reachable
        // only by accident (audit D-03, ADR 0010). The message says how to get one.
        report.error(IssueCode::config_missing_required, root.location_of("project_requirements"),
                     "'project_requirements' is required in config v2; tools/convert-config fills "
                     "it in from an upstream config, and schemas/v2/config/project_requirements.json "
                     "documents every default");
        return std::nullopt;
    }

    cursor->reject_unknown_members({"demographics", "income", "physical_activity", "risk_factors",
                                    "trend", "two_stage"});

    ProjectRequirements result;

    if (const auto demographics = cursor->object("demographics")) {
        demographics->reject_unknown_members(
            {"age", "gender", "region", "ethnicity", "max_age_for_linear_models", "gender2"});
        result.demographics.age = demographics->boolean_or_default("age", true);
        result.demographics.gender = demographics->boolean_or_default("gender", true);
        result.demographics.region = demographics->boolean_or_default("region", false);
        result.demographics.ethnicity = demographics->boolean_or_default("ethnicity", false);

        if (demographics->has("max_age_for_linear_models")) {
            const auto value = demographics->integer("max_age_for_linear_models");
            if (value.has_value()) {
                if (*value < 1) {
                    demographics->error("max_age_for_linear_models", IssueCode::config_bad_value,
                                        fmt::format("must be at least 1, or omitted for no cap; "
                                                    "found {}",
                                                    *value));
                } else {
                    result.demographics.max_age_for_linear_models = *value;
                }
            }
        }

        result.demographics.gender2 = demographics->string_or_default("gender2", "male");
        if (result.demographics.gender2 != "male" && result.demographics.gender2 != "female") {
            demographics->error("gender2", IssueCode::config_bad_value,
                                fmt::format("must be 'male' or 'female', found '{}'",
                                            result.demographics.gender2));
        }
    }

    if (const auto income = cursor->object("income")) {
        income->reject_unknown_members({"enabled", "type", "categories", "adjust_to_factors_mean",
                                        "trended", "income_based_csv_output"});
        result.income.enabled = income->boolean_or_default("enabled", true);
        result.income.type = income->string_or_default("type", "categorical");
        result.income.categories = income->string_or_default("categories", "3");
        result.income.adjust_to_factors_mean =
            income->boolean_or_default("adjust_to_factors_mean", false);
        result.income.trended = income->boolean_or_default("trended", false);
        result.income.income_based_csv_output =
            income->boolean_or_default("income_based_csv_output", true);

        if (result.income.type != "continuous" && result.income.type != "categorical") {
            income->error("type", IssueCode::config_bad_value,
                          fmt::format("must be 'continuous' or 'categorical', found '{}'",
                                      result.income.type));
        }
        if (result.income.categories != "3" && result.income.categories != "4" &&
            result.income.categories != "5") {
            income->error("categories", IssueCode::config_bad_value,
                          fmt::format("must be \"3\", \"4\" or \"5\", found '{}'",
                                      result.income.categories));
        }
    }

    if (const auto activity = cursor->object("physical_activity")) {
        activity->reject_unknown_members(
            {"enabled", "type", "adjust_to_factors_mean", "trended"});
        result.physical_activity.enabled = activity->boolean_or_default("enabled", true);
        result.physical_activity.type = activity->string_or_default("type", "simple");
        result.physical_activity.adjust_to_factors_mean =
            activity->boolean_or_default("adjust_to_factors_mean", false);
        result.physical_activity.trended = activity->boolean_or_default("trended", false);

        if (result.physical_activity.type != "simple" &&
            result.physical_activity.type != "continuous") {
            activity->error("type", IssueCode::config_bad_value,
                            fmt::format("must be 'simple' or 'continuous', found '{}'",
                                        result.physical_activity.type));
        }
    }

    if (const auto factors = cursor->object("risk_factors")) {
        factors->reject_unknown_members({"adjust_to_factors_mean", "trended"});
        result.risk_factors.adjust_to_factors_mean =
            factors->boolean_or_default("adjust_to_factors_mean", true);
        result.risk_factors.trended = factors->boolean_or_default("trended", true);
    }

    if (const auto trend = cursor->object("trend")) {
        trend->reject_unknown_members({"enabled", "type"});
        result.trend.enabled = trend->boolean_or_default("enabled", false);
        result.trend.type = trend->string_or_default("type", "null");

        static const std::set<std::string> allowed{"null", "trend", "upf_trend", "UPFTrend",
                                                   "income_trend"};
        if (!allowed.contains(result.trend.type)) {
            trend->error("type", IssueCode::config_bad_value,
                         fmt::format("must be one of null, trend, upf_trend, UPFTrend or "
                                     "income_trend, found '{}'",
                                     result.trend.type));
        }
    }

    if (const auto two_stage = cursor->object("two_stage")) {
        two_stage->reject_unknown_members({"use_logistic", "logistic_file"});
        result.two_stage.use_logistic = two_stage->boolean_or_default("use_logistic", false);
        if (two_stage->has("logistic_file")) {
            result.two_stage.logistic_file = two_stage->string("logistic_file").value_or("");
        }
        if (result.two_stage.use_logistic && result.two_stage.logistic_file.empty()) {
            two_stage->error("logistic_file", IssueCode::config_missing_required,
                             "required when 'use_logistic' is true");
        }
    }

    if (report.error_count() != before) {
        return std::nullopt;
    }

    return result;
}

std::optional<DataSpec> load_data(const JsonCursor &root, diag::IssueReport &report) {
    const auto cursor = root.object("data");
    if (!cursor.has_value()) {
        return std::nullopt;
    }

    cursor->reject_unknown_members({"source", "checksum"});

    DataSpec result;
    const auto source = cursor->string("source");
    if (!source.has_value()) {
        return std::nullopt;
    }

    std::vector<std::string> undefined;
    result.source = io::expand_environment_variables(*source, undefined);
    for (const auto &name : undefined) {
        report.error(IssueCode::config_undefined_variable, cursor->location_of("source"),
                     fmt::format("environment variable '{}' is not set", name));
    }
    if (!undefined.empty()) {
        return std::nullopt;
    }

    if (cursor->has("checksum")) {
        const auto checksum = cursor->string("checksum");
        if (!checksum.has_value()) {
            return std::nullopt;
        }
        result.checksum = core::to_lower(*checksum);
    }

    // The requirement itself lives in io::DataSource, which knows what the source turned out to
    // be; saying it here as well would mean two places to keep in step.
    return result;
}

bool load_inputs(const JsonCursor &root, const std::filesystem::path &root_path,
                 const LoadOptions &options, Config &config, diag::IssueReport &report) {
    const auto before = report.error_count();

    const auto inputs = root.object("inputs");
    if (!inputs.has_value()) {
        return false;
    }
    inputs->reject_unknown_members({"dataset", "settings"});

    if (const auto dataset = inputs->object("dataset")) {
        dataset->reject_unknown_members({"name", "format", "delimiter", "encoding", "columns"});

        if (const auto name =
                resolve_path(*dataset, "name", root_path, options.require_files_exist, report)) {
            config.dataset.name = *name;
        }

        config.dataset.format = dataset->string_or_default("format", "csv");
        config.dataset.delimiter = dataset->string_or_default("delimiter", ",");
        config.dataset.encoding = dataset->string_or_default("encoding", "ASCII");

        if (config.dataset.format != "csv") {
            dataset->error("format", IssueCode::config_bad_value,
                           fmt::format("the only supported dataset format is 'csv', found '{}'",
                                       config.dataset.format));
        }
        if (config.dataset.delimiter.size() != 1) {
            dataset->error("delimiter", IssueCode::config_bad_value,
                           fmt::format("must be a single character, found '{}'",
                                       config.dataset.delimiter));
        }

        if (const auto columns = dataset->object("columns")) {
            if (columns->node().empty()) {
                columns->error("", IssueCode::config_bad_value,
                               "at least one column must be declared");
            }
            for (const auto &member : columns->node().items()) {
                if (!member.value().is_string()) {
                    columns->error(member.key(), IssueCode::config_wrong_type,
                                   "a column's type must be a string");
                    continue;
                }
                config.dataset.columns.push_back(
                    io::CsvColumnSpec{member.key(), member.value().get<std::string>()});
            }
        }
    }

    if (const auto settings = inputs->object("settings")) {
        settings->reject_unknown_members({"country_code", "size_fraction", "age_range"});

        config.settings.country_code = settings->string("country_code").value_or("");

        if (const auto fraction = settings->number("size_fraction")) {
            if (*fraction <= 0.0 || *fraction > 1.0) {
                settings->error("size_fraction", IssueCode::config_bad_value,
                                fmt::format("must be in (0, 1], found {}", *fraction));
            } else {
                config.settings.size_fraction = *fraction;
            }
        }

        if (const auto range = settings->number_array("age_range")) {
            if (range->size() != 2) {
                settings->error("age_range", IssueCode::config_bad_value,
                                fmt::format("must be [lower, upper]; found {} value{}",
                                            range->size(), range->size() == 1 ? "" : "s"));
            } else if ((*range)[0] > (*range)[1]) {
                settings->error("age_range", IssueCode::config_bad_value,
                                fmt::format("lower bound {} is above upper bound {}", (*range)[0],
                                            (*range)[1]));
            } else {
                config.settings.age_range = core::IntegerInterval{static_cast<int>((*range)[0]),
                                                                  static_cast<int>((*range)[1])};
            }
        }
    }

    return report.error_count() == before;
}

namespace {

bool load_baseline_adjustments(const JsonCursor &modelling,
                               const std::filesystem::path &root_path,
                               const LoadOptions &options, BaselineAdjustments &result,
                               diag::IssueReport &report) {
    const auto before = report.error_count();

    const auto cursor = modelling.object("baseline_adjustments");
    if (!cursor.has_value()) {
        return false;
    }

    cursor->reject_unknown_members(
        {"format", "delimiter", "encoding", "file_names", "income_stratum_factors_mean"});

    result.format = cursor->string_or_default("format", "csv");
    result.delimiter = cursor->string_or_default("delimiter", ",");
    result.encoding = cursor->string_or_default("encoding", "ASCII");

    if (const auto names = cursor->object("file_names")) {
        names->reject_unknown_members({"factorsmean_male", "factorsmean_female"});
        for (const auto *role : {"factorsmean_male", "factorsmean_female"}) {
            if (const auto path =
                    resolve_path(*names, role, root_path, options.require_files_exist, report)) {
                result.file_names[role] = *path;
            }
        }
    }

    if (const auto strata = cursor->optional_object("income_stratum_factors_mean")) {
        strata->reject_unknown_members({"enabled", "adjustment_income_stratum_count", "strata"});

        result.income_stratum_factors_mean.enabled = strata->boolean_or_default("enabled", false);

        if (strata->has("adjustment_income_stratum_count")) {
            const auto count = strata->integer("adjustment_income_stratum_count");
            if (count.has_value()) {
                if (*count < 0) {
                    strata->error("adjustment_income_stratum_count", IssueCode::config_bad_value,
                                  fmt::format("must not be negative, found {}", *count));
                } else {
                    result.income_stratum_factors_mean.adjustment_income_stratum_count =
                        static_cast<std::size_t>(*count);
                }
            }
        }

        if (strata->has("strata")) {
            for (const auto &entry : strata->array("strata")) {
                entry.reject_unknown_members({"id", "factorsmean_male", "factorsmean_female"});

                IncomeStratumEntry stratum;
                stratum.id = entry.string("id").value_or("");
                if (const auto male = resolve_path(entry, "factorsmean_male", root_path,
                                                   options.require_files_exist, report)) {
                    stratum.factorsmean_male = *male;
                }
                if (const auto female = resolve_path(entry, "factorsmean_female", root_path,
                                                     options.require_files_exist, report)) {
                    stratum.factorsmean_female = *female;
                }
                result.income_stratum_factors_mean.strata.push_back(std::move(stratum));
            }
        }

        // Only checked when enabled: a disabled block may carry a mismatched count, which is what
        // the baseline does and what a config keeps while the feature is switched off.
        if (result.income_stratum_factors_mean.enabled &&
            result.income_stratum_factors_mean.adjustment_income_stratum_count !=
                result.income_stratum_factors_mean.strata.size()) {
            strata->error("adjustment_income_stratum_count", IssueCode::config_bad_value,
                          fmt::format("is {} but {} stratum entr{} given; they must match when "
                                      "the block is enabled",
                                      result.income_stratum_factors_mean
                                          .adjustment_income_stratum_count,
                                      result.income_stratum_factors_mean.strata.size(),
                                      result.income_stratum_factors_mean.strata.size() == 1
                                          ? "y is"
                                          : "ies are"));
        }
    }

    return report.error_count() == before;
}

} // namespace

bool load_modelling(const JsonCursor &root, const std::filesystem::path &root_path,
                    const LoadOptions &options, Config &config, diag::IssueReport &report) {
    const auto before = report.error_count();

    const auto modelling = root.object("modelling");
    if (!modelling.has_value()) {
        return false;
    }

    modelling->reject_unknown_members({"ses_model", "policy_start_year", "risk_factors",
                                       "risk_factor_models", "baseline_adjustments",
                                       "demographic_models"});

    if (const auto ses = modelling->object("ses_model")) {
        ses->reject_unknown_members({"function_name", "function_parameters"});
        config.modelling.ses_model.function_name = ses->string("function_name").value_or("");
        config.modelling.ses_model.function_parameters =
            ses->number_array("function_parameters").value_or(std::vector<double>{});

        if (config.modelling.ses_model.function_name != "normal") {
            ses->error("function_name", IssueCode::config_bad_value,
                       fmt::format("the only supported SES function is 'normal', found '{}'",
                                   config.modelling.ses_model.function_name));
        }
        if (config.modelling.ses_model.function_parameters.size() != 2) {
            ses->error("function_parameters", IssueCode::config_bad_value,
                       "the 'normal' SES function takes two parameters: mean and standard "
                       "deviation");
        } else if (config.modelling.ses_model.function_parameters[1] <= 0.0) {
            ses->error("function_parameters", IssueCode::config_bad_value,
                       "the SES standard deviation must be greater than zero");
        }
    }

    if (modelling->has("policy_start_year")) {
        const auto year = modelling->integer("policy_start_year");
        if (year.has_value()) {
            if (*year < 0) {
                modelling->error("policy_start_year", IssueCode::config_bad_value,
                                 "must not be negative");
            } else {
                config.modelling.policy_start_year = static_cast<unsigned int>(*year);
            }
        }
    }

    std::set<std::string> factor_names;
    for (const auto &entry : modelling->array("risk_factors")) {
        entry.reject_unknown_members({"name", "level", "range"});

        RiskFactorSpec factor;
        factor.name = entry.string("name").value_or("");
        factor.level = entry.integer("level").value_or(-1);

        if (factor.level < 0) {
            entry.error("level", IssueCode::config_bad_value,
                        "a risk factor's level is required and must not be negative");
        }

        if (entry.has("range")) {
            if (const auto range = entry.number_array("range")) {
                if (range->size() != 2) {
                    entry.error("range", IssueCode::config_bad_value,
                                "must be [lower, upper]");
                } else if ((*range)[0] > (*range)[1]) {
                    entry.error("range", IssueCode::config_bad_value,
                                fmt::format("lower bound {} is above upper bound {}", (*range)[0],
                                            (*range)[1]));
                } else {
                    factor.range = core::DoubleInterval{(*range)[0], (*range)[1]};
                }
            }
        }

        if (!factor.name.empty() && !factor_names.insert(core::to_lower(factor.name)).second) {
            entry.error("name", IssueCode::config_bad_value,
                        fmt::format("risk factor '{}' is declared more than once", factor.name));
        }

        config.modelling.risk_factors.push_back(std::move(factor));
    }

    if (config.modelling.risk_factors.empty()) {
        modelling->error("risk_factors", IssueCode::config_bad_value,
                         "at least one risk factor must be declared");
    }

    if (const auto models = modelling->object("risk_factor_models")) {
        models->reject_unknown_members({"static", "dynamic"});
        for (const auto *kind : {"static", "dynamic"}) {
            if (const auto path =
                    resolve_path(*models, kind, root_path, options.require_files_exist, report)) {
                config.modelling.risk_factor_models[kind] = *path;
            }
        }
    }

    load_baseline_adjustments(*modelling, root_path, options, config.modelling.baseline_adjustments,
                              report);

    if (const auto demographics = modelling->optional_object("demographic_models")) {
        config.modelling.demographic_models = demographics->node();
    }

    return report.error_count() == before;
}

namespace {

std::optional<InterventionSpec> load_intervention(const JsonCursor &types,
                                                  const std::string &identifier) {
    const auto cursor = types.object(identifier);
    if (!cursor.has_value()) {
        return std::nullopt;
    }

    cursor->reject_unknown_members({"active_period", "impacts", "impact_type", "dynamics",
                                    "coefficients", "coverage_rates", "coverage_cutoff_time",
                                    "child_cutoff_age", "adjustments"});

    InterventionSpec result;
    result.identifier = core::to_lower(identifier);

    if (const auto period = cursor->object("active_period")) {
        period->reject_unknown_members({"start_time", "finish_time"});
        result.active_period.start_time = period->integer("start_time").value_or(0);

        const auto *finish = period->node().find("finish_time") == period->node().end()
                                 ? nullptr
                                 : &*period->node().find("finish_time");
        if (finish == nullptr) {
            period->error("finish_time", IssueCode::config_missing_required,
                          "required; use null for 'no end'");
        } else if (!finish->is_null()) {
            result.active_period.finish_time = period->integer("finish_time");
        }

        // The baseline's PolicyInterval rejects a negative start in its constructor; here the
        // config is the only place one can come from, so it is checked here. A year before the
        // run's start is not an error — an intervention already running when the simulation opens
        // is a real case — but a negative year is not a year.
        if (result.active_period.start_time < 0) {
            period->error("start_time", IssueCode::config_bad_value,
                          fmt::format("{} is not a calendar year",
                                      result.active_period.start_time));
        }

        if (result.active_period.finish_time.has_value() &&
            *result.active_period.finish_time < result.active_period.start_time) {
            period->error("finish_time", IssueCode::config_bad_value,
                          fmt::format("{} is before start_time {}",
                                      *result.active_period.finish_time,
                                      result.active_period.start_time));
        }
    }

    if (cursor->has("impact_type")) {
        result.impact_type = cursor->string("impact_type").value_or("");
    }

    for (const auto &impact : cursor->array("impacts")) {
        impact.reject_unknown_members({"risk_factor", "impact_value", "from_age", "to_age"});

        PolicyImpact entry;
        entry.risk_factor = impact.string("risk_factor").value_or("");
        entry.impact_value = impact.number("impact_value").value_or(0.0);
        entry.from_age = impact.unsigned_integer("from_age").value_or(0);

        const auto to_age = impact.node().find("to_age");
        if (to_age != impact.node().end() && !to_age->is_null()) {
            entry.to_age = impact.unsigned_integer("to_age");
            if (entry.to_age.has_value() && *entry.to_age < entry.from_age) {
                impact.error("to_age", IssueCode::config_bad_value,
                             fmt::format("{} is below from_age {}", *entry.to_age, entry.from_age));
            }
        }

        result.impacts.push_back(std::move(entry));
    }

    if (result.impacts.empty()) {
        // Upstream ships exactly this in four of its six examples: `simple` with an empty impact
        // list, selected as the active intervention. It is a well-defined no-op — the
        // intervention scenario then reproduces the baseline scenario — and it is a useful
        // calibration run, so it is accepted. It is also a plausible mistake, so it is said out
        // loud.
        cursor->warning("impacts", IssueCode::config_bad_value,
                        "this intervention has no impacts, so the intervention scenario will "
                        "reproduce the baseline scenario exactly");
    }

    if (cursor->has("dynamics")) {
        result.dynamics = cursor->number_array("dynamics").value_or(std::vector<double>{});
    }
    if (cursor->has("coefficients")) {
        result.coefficients = cursor->number_array("coefficients").value_or(std::vector<double>{});
    }
    if (cursor->has("coverage_rates")) {
        result.coverage_rates =
            cursor->number_array("coverage_rates").value_or(std::vector<double>{});
    }
    if (cursor->has("coverage_cutoff_time")) {
        result.coverage_cutoff_time = cursor->unsigned_integer("coverage_cutoff_time");
    }
    if (cursor->has("child_cutoff_age")) {
        result.child_cutoff_age = cursor->unsigned_integer("child_cutoff_age");
    }
    if (cursor->has("adjustments")) {
        for (const auto &adjustment : cursor->array("adjustments")) {
            adjustment.reject_unknown_members({"risk_factor", "value"});
            result.adjustments.push_back(
                PolicyAdjustment{.risk_factor = adjustment.string("risk_factor").value_or(""),
                                 .value = adjustment.number("value").value_or(0.0)});
        }
    }

    return result;
}

} // namespace

bool load_running(const JsonCursor &root, Config &config, diag::IssueReport &report) {
    const auto before = report.error_count();

    const auto running = root.object("running");
    if (!running.has_value()) {
        return false;
    }

    running->reject_removed_member(
        "sync_timeout_ms",
        "scenarios now run one after the other and the baseline's net migration travels in a "
        "journal, so there is nothing to wait for");
    running->reject_unknown_members(
        {"seed", "start_time", "stop_time", "trial_runs", "diseases", "interventions"});

    // Required, and a scalar. The baseline's array form allowed zero elements, which meant an
    // unseeded, irreproducible run whose results file then recorded the seed as 0 (audit B-06).
    if (const auto *seed = running->node().find("seed") == running->node().end()
                               ? nullptr
                               : &*running->node().find("seed");
        seed != nullptr && seed->is_array()) {
        running->error("seed", IssueCode::config_wrong_type,
                       "must be a single integer in config v2, not an array; an empty array meant "
                       "an unseeded run, which this build does not do");
    } else if (const auto value = running->integer("seed")) {
        if (*value < 0) {
            running->error("seed", IssueCode::config_bad_value, "must not be negative");
        } else {
            config.running.seed = static_cast<std::uint32_t>(*value);
        }
    }

    config.running.start_time = running->unsigned_integer("start_time").value_or(0);
    config.running.stop_time = running->unsigned_integer("stop_time").value_or(0);

    if (config.running.stop_time <= config.running.start_time) {
        running->error("stop_time", IssueCode::config_bad_value,
                       fmt::format("{} must be after start_time {}", config.running.stop_time,
                                   config.running.start_time));
    }

    if (running->has("trial_runs")) {
        const auto runs = running->unsigned_integer("trial_runs");
        if (runs.has_value()) {
            if (*runs == 0) {
                running->error("trial_runs", IssueCode::config_bad_value, "must be at least 1");
            } else {
                config.running.trial_runs = *runs;
            }
        }
    } else {
        config.running.trial_runs =
            static_cast<unsigned int>(running->integer_or_default("trial_runs", 1));
    }

    if (const auto diseases = running->string_array("diseases")) {
        std::set<std::string> seen;
        for (const auto &disease : *diseases) {
            if (!seen.insert(core::to_lower(disease)).second) {
                running->error("diseases", IssueCode::config_bad_value,
                               fmt::format("disease '{}' is selected more than once", disease));
                continue;
            }
            config.running.diseases.push_back(core::to_lower(disease));
        }
        if (config.running.diseases.empty()) {
            running->error("diseases", IssueCode::config_bad_value,
                           "at least one disease must be selected");
        }
    }

    if (const auto interventions = running->object("interventions")) {
        interventions->reject_unknown_members({"active_type_id", "types"});

        // `types` is read whether or not one is active, so that a baseline-only config still has
        // its intervention definitions validated — and so that switching one on later does not
        // discover a broken definition at that point.
        const auto types = interventions->object("types");

        const auto active = interventions->node().find("active_type_id");
        if (active == interventions->node().end()) {
            interventions->error("active_type_id", IssueCode::config_missing_required,
                                 "required; use null for a baseline-only run");
        } else if (!active->is_null()) {
            if (!active->is_string()) {
                interventions->error("active_type_id", IssueCode::config_wrong_type,
                                     "expected a string or null");
            } else if (types.has_value()) {
                const auto identifier = active->get<std::string>();

                // Case-insensitive, because the upstream examples are inconsistent about it.
                std::string matched;
                for (const auto &member : types->node().items()) {
                    if (core::case_insensitive::equals(member.key(), identifier)) {
                        matched = member.key();
                        break;
                    }
                }

                if (matched.empty()) {
                    std::vector<std::string> available;
                    for (const auto &member : types->node().items()) {
                        available.push_back(member.key());
                    }
                    interventions->error(
                        "active_type_id", IssueCode::config_bad_value,
                        fmt::format("'{}' is not defined under 'types' (defined: {})", identifier,
                                    available.empty() ? "none"
                                                      : fmt::format("{}", fmt::join(available, ", "))));
                } else {
                    config.running.active_intervention = load_intervention(*types, matched);

                    // The six upstream identifiers. An identifier outside this set stops the run
                    // with a sentence rather than being silently ignored (ADR 0021).
                    static const std::vector<std::string> implemented{
                        "simple",   "marketing",         "dynamic_marketing",
                        "fiscal",   "physical_activity", "food_labelling"};

                    if (config.running.active_intervention.has_value() &&
                        std::find(implemented.begin(), implemented.end(),
                                  config.running.active_intervention->identifier) ==
                            implemented.end()) {
                        interventions->error(
                            "active_type_id", IssueCode::feature_not_implemented,
                            fmt::format("intervention '{}' is not implemented in this build; the "
                                        "six that are: {}. See docs/backlog.md",
                                        config.running.active_intervention->identifier,
                                        fmt::join(implemented, ", ")));
                    }
                }
            }
        }
    }

    return report.error_count() == before;
}

bool load_output(const JsonCursor &root, const LoadOptions &options, Config &config,
                 diag::IssueReport &report) {
    const auto before = report.error_count();

    const auto output = root.object("output");
    if (!output.has_value()) {
        return false;
    }

    output->reject_unknown_members(
        {"comorbidities", "folder", "file_name", "individual_id_tracking"});

    config.output.comorbidities = output->unsigned_integer("comorbidities").value_or(0);

    const auto folder = output->string("folder");
    const auto configured_folder = folder.value_or("");

    if (options.output_folder.has_value() && options.output_folder_override.has_value()) {
        report.error(IssueCode::config_bad_value, IssueLocation{.field = "--output"},
                     "an output folder was given both as a command-line override and as a host "
                     "override; give it in one place");
    } else if (options.output_folder_override.has_value()) {
        // The host decides, and the config does not get a vote — not even a warning, because a
        // warning on every run of a host that always sets this is a warning nobody reads.
        config.output.folder = expand_folder(*options.output_folder_override,
                                             IssueLocation{.field = "output_folder_override"},
                                             report);
    } else if (options.output_folder.has_value() && !configured_folder.empty()) {
        output->error("folder", IssueCode::config_bad_value,
                      "an output folder was given both here and on the command line; give it in "
                      "one place");
    } else if (options.output_folder.has_value()) {
        config.output.folder =
            expand_folder(*options.output_folder, IssueLocation{.field = "--output"}, report);
    } else if (!configured_folder.empty()) {
        config.output.folder =
            expand_folder(configured_folder, output->location_of("folder"), report);
    } else if (folder.has_value()) {
        output->error("folder", IssueCode::config_bad_value,
                      "an output folder is required, here or with --output");
    }

    config.output.file_name = output->string("file_name").value_or("");
    if (config.output.file_name.empty() && output->has("file_name")) {
        output->error("file_name", IssueCode::config_bad_value, "must not be empty");
    }

    if (const auto tracking = output->optional_object("individual_id_tracking")) {
        tracking->reject_unknown_members({"enabled", "age_min", "age_max", "gender", "regions",
                                          "ethnicities", "risk_factors", "years", "scenarios"});

        IndividualTracking result;
        result.enabled = tracking->boolean_or_default("enabled", false);

        for (const auto &[field, target] :
             {std::pair{"age_min", &result.age_min}, std::pair{"age_max", &result.age_max}}) {
            const auto found = tracking->node().find(field);
            if (found != tracking->node().end() && !found->is_null()) {
                *target = tracking->integer(field);
            }
        }

        if (result.age_min.has_value() && result.age_max.has_value() &&
            *result.age_min > *result.age_max) {
            tracking->error("age_max", IssueCode::config_bad_value,
                            fmt::format("{} is below age_min {}", *result.age_max,
                                        *result.age_min));
        }

        result.gender = tracking->string_or_default("gender", "all");
        if (result.gender != "male" && result.gender != "female" && result.gender != "all") {
            tracking->error("gender", IssueCode::config_bad_value,
                            fmt::format("must be 'male', 'female' or 'all', found '{}'",
                                        result.gender));
        }

        result.scenarios = tracking->string_or_default("scenarios", "both");
        if (result.scenarios != "baseline" && result.scenarios != "intervention" &&
            result.scenarios != "both") {
            tracking->error("scenarios", IssueCode::config_bad_value,
                            fmt::format("must be 'baseline', 'intervention' or 'both', found '{}'",
                                        result.scenarios));
        }

        if (tracking->has("regions")) {
            result.regions = tracking->string_array("regions").value_or(std::vector<std::string>{});
        }
        if (tracking->has("ethnicities")) {
            result.ethnicities =
                tracking->string_array("ethnicities").value_or(std::vector<std::string>{});
        }
        if (tracking->has("risk_factors")) {
            result.risk_factors =
                tracking->string_array("risk_factors").value_or(std::vector<std::string>{});
        }
        if (tracking->has("years")) {
            for (const auto value : tracking->number_array("years").value_or(
                     std::vector<double>{})) {
                result.years.push_back(static_cast<int>(value));
            }
        }

        config.output.individual_id_tracking = std::move(result);
    }

    return report.error_count() == before;
}

} // namespace detail

std::string expand_output_file_name(const Output &output, int job_id) {
    auto name = output.file_name;

    const auto replace = [&name](std::string_view token, const std::string &value) {
        for (auto position = name.find(token); position != std::string::npos;
             position = name.find(token, position + value.size())) {
            name.replace(position, token.size(), value);
        }
    };

    if (name.find("{TIMESTAMP}") != std::string::npos) {
        replace("{TIMESTAMP}", utc_timestamp());
    }

    if (name.find("{JOBID}") != std::string::npos) {
        replace("{JOBID}", std::to_string(job_id));
    } else if (job_id > 0) {
        // A job id that the name does not mention would otherwise have every array element of an
        // HPC job writing to the same file.
        const std::filesystem::path path{name};
        name = fmt::format("{}_{}{}", path.stem().string(), job_id, path.extension().string());
    }

    return name;
}

std::optional<Config> load_from_json(const nlohmann::json &document,
                                     const std::filesystem::path &root_path,
                                     const LoadOptions &options, diag::IssueReport &report) {
    const auto before = report.error_count();

    if (!document.is_object()) {
        report.error(IssueCode::config_wrong_type, IssueLocation{},
                     "the config must be a JSON object");
        return std::nullopt;
    }

    const JsonCursor root{document, "", "", report};

    // "$comment" is allowed anywhere a document takes one: it is the conventional way to
    // annotate JSON that has no schema slot for prose.
    root.reject_unknown_members({"$schema", "$comment", "version", "project_requirements", "data",
                                 "inputs", "modelling", "running", "output",
                                 "population_impact_fraction"});

    // Removed in config v2, with the replacement named rather than the key ignored.
    root.reject_removed_member("trend_type", "use project_requirements.trend instead");
    root.reject_removed_member("income_categories",
                               "use project_requirements.income.categories instead");

    Config config;
    config.root_path = root_path;
    config.job_id = options.job_id;
    config.verbosity = options.verbose ? core::VerboseMode::verbose : core::VerboseMode::none;

    detail::check_version(root, report);

    if (const auto requirements = detail::load_project_requirements(root, report)) {
        config.project_requirements = *requirements;
    }
    if (const auto data = detail::load_data(root, report)) {
        config.data = *data;
    }

    detail::load_inputs(root, root_path, options, config, report);
    detail::load_modelling(root, root_path, options, config, report);
    detail::load_running(root, config, report);
    detail::load_output(root, options, config, report);

    // Population impact fraction (ADR 0038). Implemented since this run; the tables themselves are
    // read from the data store, so all that happens here is validating what the config asks for.
    if (const auto pif = root.optional_object("population_impact_fraction")) {
        pif->reject_unknown_members({"enabled", "data_root_path", "risk_factor", "scenario"});

        PopulationImpactFraction result;
        result.enabled = pif->boolean_or_default("enabled", false);

        if (result.enabled) {
            result.risk_factor = pif->string("risk_factor").value_or("");
            result.scenario = pif->string("scenario").value_or("");

            // Both become directory names under the data store, so both have to be usable as one. A
            // path separator here would let a config reach outside the store it declared.
            for (const auto &[field, value] : {std::pair{"risk_factor", &result.risk_factor},
                                                std::pair{"scenario", &result.scenario}}) {
                if (value->empty()) {
                    pif->error(field, IssueCode::config_missing_required,
                               "required when population impact fraction is enabled");
                } else if (value->find('/') != std::string::npos ||
                           value->find('\\') != std::string::npos || *value == "." ||
                           *value == "..") {
                    pif->error(field, IssueCode::config_bad_value,
                               "names a directory inside the data store, so it must not contain a "
                               "path separator");
                }
            }

            // Dead upstream and dead here, and saying so is the point: the field is required by
            // upstream's schema and has its ${VAR}s expanded, and is then overwritten with the data
            // store's own root before it is used (repository.cpp:118). A config that points it
            // somewhere else has been misled about what it does.
            if (pif->has("data_root_path")) {
                pif->warning("data_root_path", IssueCode::config_default_applied,
                             "ignored: the population impact fraction tables are read from the data "
                             "store that `data.source` names, which is what upstream does too — it "
                             "overwrites this field with the store's root before using it. Remove it");
            }
        }

        config.population_impact_fraction = result;
    }

    if (report.error_count() != before) {
        return std::nullopt;
    }

    return config;
}

std::optional<Config> load(const std::filesystem::path &path, const LoadOptions &options,
                           diag::IssueReport &report) {
    const auto document = io::read_json(path, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto root_path = std::filesystem::absolute(path).parent_path();

    // The file name has to reach every issue raised about the document, and JsonCursor carries
    // the file it was given, so re-root the cursor's file here by validating against a copy that
    // knows the path.
    diag::IssueReport document_report;
    const JsonCursor root{*document, path.string(), "", document_report};
    (void)root;

    auto config = load_from_json(*document, root_path, options, report);
    if (!config.has_value()) {
        return std::nullopt;
    }

    config->source_path = std::filesystem::absolute(path);
    try {
        config->source_sha256 = io::sha256_file(config->source_path);
    } catch (const std::exception &error) {
        report.warning(IssueCode::file_unreadable, IssueLocation{.file = path.string()},
                       fmt::format("could not hash the config file for the run metadata: {}",
                                   error.what()));
    }

    return config;
}

} // namespace hgps::config
