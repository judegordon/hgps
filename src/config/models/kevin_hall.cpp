// The `KevinHall` dynamic model: nutrients, foods, weight quantiles, energy/activity quantiles
// and the height regression.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: load_kevinhall_risk_model_definition and its CSV helpers in
//         src/HealthGPS.Input/model_parser.cpp.
#include "model_loader.h"

#include "core/chars.h"
#include "core/string_util.h"
#include "io/csv_reader.h"
#include "io/json.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace hgps::config::models::detail {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

struct FileBlock {
    std::filesystem::path path;
    io::CsvOptions options;

    /// @brief The column names the block declares, in the order it declares them.
    ///
    /// These files carry columns nothing reads — an R `write.csv` row-index column, unnamed, is
    /// the first column of every quantile file in the FINCH pack — so which column holds the data
    /// is a fact the model file states rather than a position to assume.
    std::vector<std::string> columns;
};

std::optional<FileBlock> read_file_block(const io::JsonCursor &cursor, const std::string &field,
                                         const std::filesystem::path &root) {
    const auto block = cursor.object(field);
    if (!block.has_value()) {
        return std::nullopt;
    }

    block->reject_unknown_members({"name", "format", "delimiter", "encoding", "columns"});

    const auto name = block->string("name");
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

    if (const auto columns = block->optional_object("columns")) {
        for (const auto &member : columns->node().items()) {
            result.columns.push_back(member.key());
        }
    }

    return result;
}

/// @brief The index of the one column a quantile file's block declares.
std::optional<std::size_t> declared_column(const io::CsvDocument &document, const FileBlock &file,
                                           const std::string &fallback,
                                           diag::IssueReport &report) {
    const auto wanted = file.columns.size() == 1 ? file.columns.front() : fallback;
    if (const auto index = document.column_index(wanted)) {
        return index;
    }

    report.error(IssueCode::csv_missing_column, IssueLocation{.file = file.path.string()},
                 fmt::format("no column named '{}'; the file has [{}]", wanted,
                             fmt::join(document.headers(), ", ")));
    return std::nullopt;
}

/// @brief A single-column CSV of quantiles, sorted.
std::optional<std::vector<double>> read_quantiles(const FileBlock &file, const std::string &what,
                                                  diag::IssueReport &report) {
    const auto document = io::read_csv(file.path, file.options, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    if (document->num_rows() == 0) {
        report.error(IssueCode::csv_bad_value, IssueLocation{.file = file.path.string()},
                     fmt::format("the {} file has no data rows", what));
        return std::nullopt;
    }

    const auto column = declared_column(*document, file, "quantile", report);
    if (!column.has_value()) {
        return std::nullopt;
    }

    std::vector<double> values;
    values.reserve(document->num_rows());
    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto value = document->field_as_double(row, *column, report);
        if (!value.has_value()) {
            return std::nullopt;
        }
        values.push_back(*value);
    }

    // Sorted here rather than trusted: the quantile lookup is a binary search, and an unsorted
    // curve would silently give the wrong answer instead of failing.
    std::sort(values.begin(), values.end());
    return values;
}

/// @brief `QuintileN` -> N-1, or nullopt.
std::optional<std::size_t> quintile_index(const std::string &key) {
    constexpr std::string_view prefix = "Quintile";
    if (!key.starts_with(prefix)) {
        return std::nullopt;
    }

    const auto suffix = key.substr(prefix.size());
    if (suffix.empty()) {
        return std::nullopt;
    }

    std::size_t value = 0;
    for (const char character : suffix) {
        if (!core::chars::is_digit(character)) {
            return std::nullopt;
        }
        value = value * 10 + static_cast<std::size_t>(character - '0');
    }

    return value == 0 ? std::nullopt : std::optional<std::size_t>{value - 1};
}

bool is_file_block(const nlohmann::json &node) {
    return node.is_object() && node.contains("name") && node.contains("format");
}

/// @brief One sex's weight-quantile curves: either one shared curve or one per income stratum.
std::optional<std::vector<std::vector<double>>>
read_weight_quantiles(const io::JsonCursor &parent, const std::string &sex,
                      const std::filesystem::path &root, bool stratified, std::size_t strata,
                      diag::IssueReport &report) {
    const auto node = parent.object(sex);
    if (!node.has_value()) {
        return std::nullopt;
    }

    if (is_file_block(node->node())) {
        const auto file = read_file_block(parent, sex, root);
        if (!file.has_value()) {
            return std::nullopt;
        }
        const auto curve = read_quantiles(*file, fmt::format("{} weight quantile", sex), report);
        if (!curve.has_value()) {
            return std::nullopt;
        }
        // One curve, broadcast to every stratum: a pack without quintile files describes one
        // population.
        return std::vector<std::vector<double>>(stratified && strata > 1 ? strata : 1, *curve);
    }

    if (!stratified) {
        node->error("", IssueCode::config_bad_value,
                    fmt::format("WeightQuantiles.{} gives one curve per quintile, but "
                                "baseline_adjustments.income_stratum_factors_mean.enabled is "
                                "false, so there are no quintiles to give them to",
                                sex));
        return std::nullopt;
    }
    if (strata < 2) {
        node->error("", IssueCode::config_bad_value,
                    fmt::format("WeightQuantiles.{} gives one curve per quintile, but "
                                "adjustment_income_stratum_count is {}",
                                sex, strata));
        return std::nullopt;
    }

    std::map<std::size_t, std::string> keys;
    for (const auto &member : node->node().items()) {
        const auto index = quintile_index(member.key());
        if (!index.has_value()) {
            node->error(member.key(), IssueCode::config_bad_value,
                        fmt::format("'{}' is not a Quintile1..N key", member.key()));
            return std::nullopt;
        }
        if (!is_file_block(member.value())) {
            node->error(member.key(), IssueCode::config_bad_value,
                        "a quintile entry must be a {name, format, …} file block");
            return std::nullopt;
        }
        keys[*index] = member.key();
    }

    if (keys.size() != strata) {
        node->error("", IssueCode::model_dimension_mismatch,
                    fmt::format("WeightQuantiles.{} gives {} quintile curve(s) but "
                                "adjustment_income_stratum_count is {}",
                                sex, keys.size(), strata));
        return std::nullopt;
    }

    std::vector<std::vector<double>> curves;
    curves.reserve(strata);
    for (std::size_t i = 0; i < strata; ++i) {
        const auto key = keys.find(i);
        if (key == keys.end()) {
            node->error("", IssueCode::model_dimension_mismatch,
                        fmt::format("WeightQuantiles.{} is missing Quintile{}", sex, i + 1));
            return std::nullopt;
        }
        const auto file = read_file_block(*node, key->second, root);
        if (!file.has_value()) {
            return std::nullopt;
        }
        const auto curve =
            read_quantiles(*file, fmt::format("{} {} weight quantile", sex, key->second), report);
        if (!curve.has_value()) {
            return std::nullopt;
        }
        curves.push_back(*curve);
    }

    return curves;
}

/// @brief The height CSV: a slope and a residual standard deviation, one row per income stratum.
///
/// Three shapes exist across the packs and all three are accepted, because all three are in use:
/// `slope,std` with a header, `,slope,std` with a header and a label column, and two bare numbers
/// with no header at all. Named columns win when there are any; otherwise the columns are taken
/// by position and the first line is data rather than a header.
std::optional<std::vector<model::HeightModelParams>>
read_height_params(const FileBlock &file, bool stratified, std::size_t strata,
                   diag::IssueReport &report) {
    const auto document = io::read_csv(file.path, file.options, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto &headers = document->headers();
    if (headers.size() != 2 && headers.size() != 3) {
        report.error(IssueCode::csv_missing_column, IssueLocation{.file = file.path.string()},
                     fmt::format("a height file needs 2 columns (slope, std) or 3 "
                                 "(label, slope, std); this one has {}",
                                 headers.size()));
        return std::nullopt;
    }

    const auto named_slope = document->column_index("slope");
    const auto named_stddev = document->column_index("std")
                                  ? document->column_index("std")
                                  : document->column_index("stddev");
    const bool has_header = named_slope.has_value() && named_stddev.has_value();

    const std::size_t slope_column = has_header ? *named_slope : (headers.size() == 3 ? 1 : 0);
    const std::size_t stddev_column = has_header ? *named_stddev : (headers.size() == 3 ? 2 : 1);

    std::vector<model::HeightModelParams> rows;

    const auto take = [&](double slope, double stddev, std::size_t line) {
        if (stddev <= 0.0) {
            report.error(IssueCode::csv_bad_value,
                         IssueLocation{.file = file.path.string(), .line = line},
                         fmt::format("the height residual standard deviation is {}; with zero or "
                                     "less every person of an age and sex would be exactly the "
                                     "expected height",
                                     stddev));
            return false;
        }
        rows.push_back(model::HeightModelParams{.slope = slope, .stddev = stddev});
        return true;
    };

    // No header means the first line is a row, and the reader has already taken it as the header.
    if (!has_header) {
        try {
            if (!take(std::stod(headers[slope_column]), std::stod(headers[stddev_column]), 1)) {
                return std::nullopt;
            }
        } catch (const std::exception &) {
            report.error(IssueCode::csv_bad_value,
                         IssueLocation{.file = file.path.string(), .line = 1},
                         fmt::format("the first line is neither a 'slope,std' header nor a pair "
                                     "of numbers: '{}', '{}'",
                                     headers[slope_column], headers[stddev_column]));
            return std::nullopt;
        }
    }

    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto slope = document->field_as_double(row, slope_column, report);
        const auto stddev = document->field_as_double(row, stddev_column, report);
        if (!slope || !stddev || !take(*slope, *stddev, document->line_of(row))) {
            return std::nullopt;
        }
    }

    if (rows.empty()) {
        report.error(IssueCode::csv_bad_value, IssueLocation{.file = file.path.string()},
                     "the height file has a header and no data rows");
        return std::nullopt;
    }

    if (rows.size() == 1) {
        return std::vector<model::HeightModelParams>(stratified && strata > 1 ? strata : 1,
                                                     rows.front());
    }

    if (stratified && rows.size() != strata) {
        report.error(IssueCode::model_dimension_mismatch,
                     IssueLocation{.file = file.path.string()},
                     fmt::format("the height file has {} rows but adjustment_income_stratum_count "
                                 "is {}; use one row to broadcast, or exactly {}",
                                 rows.size(), strata, strata));
        return std::nullopt;
    }

    if (!stratified && rows.size() > 1) {
        report.warning(IssueCode::config_default_applied,
                       IssueLocation{.file = file.path.string()},
                       fmt::format("the height file has {} rows but no income strata are "
                                   "configured, so only the first is used",
                                   rows.size()));
        rows.resize(1);
    }

    return rows;
}

} // namespace

std::unique_ptr<model::RiskFactorModel> load_kevin_hall(const nlohmann::json &document,
                                                         const std::filesystem::path &path,
                                                         const LoadContext &context,
                                                         diag::IssueReport &report) {
    const auto before = report.error_count();
    const io::JsonCursor root{document, path.string(), "", report};
    const auto root_path = path.parent_path();

    root.reject_unknown_members({"$schema", "$comment", "ModelName", "Nutrients", "Foods",
                                 "WeightQuantiles", "EnergyPhysicalActivityQuantiles", "Height",
                                 "HeightSlope", "HeightStdDev"});

    auto parameters = std::make_shared<model::KevinHallParameters>();

    for (const auto &nutrient : root.array("Nutrients")) {
        nutrient.reject_unknown_members({"Name", "Range", "Energy"});
        const auto name = nutrient.string("Name");
        const auto range = nutrient.number_array("Range");
        const auto energy = nutrient.number("Energy");
        if (!name || !range || range->size() != 2 || !energy) {
            nutrient.error("Name", IssueCode::model_missing_key,
                           "a nutrient needs a Name, a Range [min, max] and an Energy value");
            continue;
        }
        const core::Identifier key{*name};
        parameters->nutrient_ranges[key] = core::DoubleInterval{(*range)[0], (*range)[1]};
        parameters->energy_equation[key] = *energy;
    }

    if (parameters->energy_equation.empty()) {
        root.error("Nutrients", IssueCode::model_missing_key,
                   "the Kevin Hall model needs at least one nutrient");
        return nullptr;
    }

    auto trend = std::make_shared<std::map<core::Identifier, double>>();
    auto trend_steps = std::make_shared<std::map<core::Identifier, int>>();

    for (const auto &food : root.array("Foods")) {
        food.reject_unknown_members({"Name", "Nutrients", "Price", "ExpectedTrend", "TrendSteps"});
        const auto name = food.string("Name");
        if (!name.has_value()) {
            continue;
        }
        const core::Identifier key{*name};

        // A food group is a predictor of nothing, but it IS a risk factor the static model has to
        // have generated, so its name is checked against the declared set for the same reason
        // every coefficient name is.
        validate_predictor_name(*name, path, fmt::format("/Foods/{}", *name), context, report);

        (*trend)[key] = food.node().value("ExpectedTrend", 1.0);
        (*trend_steps)[key] = food.node().value("TrendSteps", 0);

        if (food.node().contains("Price") && food.node()["Price"].is_number()) {
            parameters->food_prices[key] = food.node()["Price"].get<double>();
        } else {
            parameters->food_prices[key] = std::nullopt;
        }

        const auto nutrients = food.object("Nutrients");
        if (!nutrients.has_value()) {
            continue;
        }
        for (const auto &member : nutrients->node().items()) {
            const core::Identifier nutrient{member.key()};
            if (!parameters->energy_equation.contains(nutrient)) {
                nutrients->error(member.key(), IssueCode::model_unknown_predictor,
                                 fmt::format("'{}' is not one of this model's nutrients",
                                             member.key()));
                continue;
            }
            if (!member.value().is_number()) {
                nutrients->error(member.key(), IssueCode::config_bad_value,
                                 "a nutrient content must be a number");
                continue;
            }
            parameters->nutrient_equations[key][nutrient] = member.value().get<double>();
        }
    }

    if (parameters->nutrient_equations.empty()) {
        root.error("Foods", IssueCode::model_missing_key,
                   "the Kevin Hall model needs at least one food group");
        return nullptr;
    }

    const auto &stratum_config =
        context.config->modelling.baseline_adjustments.income_stratum_factors_mean;
    const bool stratified = stratum_config.enabled;
    const auto strata = stratum_config.adjustment_income_stratum_count;

    const auto quantiles = root.object("WeightQuantiles");
    if (!quantiles.has_value()) {
        return nullptr;
    }
    quantiles->reject_unknown_members({"Male", "Female"});

    for (const auto &[sex, key] : std::vector<std::pair<core::Gender, std::string>>{
             {core::Gender::male, "Male"}, {core::Gender::female, "Female"}}) {
        const auto curves = read_weight_quantiles(*quantiles, key, root_path, stratified, strata,
                                                   report);
        if (curves.has_value()) {
            parameters->weight_quantiles[sex] = *curves;
        }
    }

    const auto epa_file = read_file_block(root, "EnergyPhysicalActivityQuantiles", root_path);
    if (epa_file.has_value()) {
        const auto values = read_quantiles(*epa_file, "energy/physical activity quantile", report);
        if (values.has_value()) {
            parameters->epa_quantiles = *values;
        }
    }

    if (root.node().contains("Height")) {
        const auto height = root.object("Height");
        if (height.has_value()) {
            height->reject_unknown_members({"Male", "Female"});
            for (const auto &[sex, key] : std::vector<std::pair<core::Gender, std::string>>{
                     {core::Gender::male, "Male"}, {core::Gender::female, "Female"}}) {
                const auto file = read_file_block(*height, key, root_path);
                if (!file.has_value()) {
                    continue;
                }
                const auto rows = read_height_params(*file, stratified, strata, report);
                if (rows.has_value()) {
                    parameters->height_params[sex] = *rows;
                }
            }
        }
    } else {
        // The older shape: two scalars per sex in the JSON rather than a CSV.
        const auto slope = root.object("HeightSlope");
        const auto stddev = root.object("HeightStdDev");
        if (!slope || !stddev) {
            root.error("Height", IssueCode::model_missing_key,
                       "the Kevin Hall model needs either a Height file block or HeightSlope and "
                       "HeightStdDev");
            return nullptr;
        }
        for (const auto &[sex, key] : std::vector<std::pair<core::Gender, std::string>>{
                 {core::Gender::male, "Male"}, {core::Gender::female, "Female"}}) {
            const auto slope_value = slope->number(key);
            const auto stddev_value = stddev->number(key);
            if (!slope_value || !stddev_value) {
                continue;
            }
            parameters->height_params[sex] = {model::HeightModelParams{
                .slope = *slope_value, .stddev = *stddev_value}};
        }
    }

    // The configured Weight range, which the energy balance is checked against.
    const core::Identifier weight{"weight"};
    if (context.mapping->contains(weight)) {
        parameters->weight_range = context.mapping->at(weight).range();
    }

    // Every food group and every nutrient must be in the FactorsMean tables, because the derived
    // expected values read them.
    for (const auto &[food, unused] : parameters->nutrient_equations) {
        for (const auto sex : {core::Gender::male, core::Gender::female}) {
            if (!context.expected->contains(sex, food)) {
                report.error(IssueCode::model_missing_key,
                             IssueLocation{.file = path.string(), .field = food.to_string()},
                             fmt::format("food group '{}' has no {} column in the FactorsMean "
                                         "tables, so its expected intake cannot be read",
                                         food.to_string(),
                                         sex == core::Gender::male ? "male" : "female"));
            }
        }
    }

    for (const auto &needed : {core::Identifier{"energyintake"}, core::Identifier{"weight"},
                               core::Identifier{"height"},
                               core::Identifier{"physicalactivity"}}) {
        for (const auto sex : {core::Gender::male, core::Gender::female}) {
            if (!context.expected->contains(sex, needed)) {
                report.error(IssueCode::model_missing_key,
                             IssueLocation{.file = path.string(), .field = needed.to_string()},
                             fmt::format("the Kevin Hall model needs a {} '{}' column in the "
                                         "FactorsMean tables",
                                         sex == core::Gender::male ? "male" : "female",
                                         needed.to_string()));
            }
        }
    }

    if (report.error_count() != before) {
        return nullptr;
    }

    return std::make_unique<model::KevinHallModel>(context.expected, trend, trend_steps,
                                                    std::move(parameters));
}

} // namespace hgps::config::models::detail
