#include "store.h"

#include "core/math_util.h"
#include "core/string_util.h"
#include "io/csv_reader.h"

#include <algorithm>
#include <map>
#include <utility>

#include <fmt/format.h>

namespace hgps::data {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

} // namespace

Store::Store(DataIndex index) : index_{std::move(index)} {}

std::optional<Store> Store::open(const std::filesystem::path &root, diag::IssueReport &report) {
    auto index = DataIndex::load(root, report);
    if (!index.has_value()) {
        return std::nullopt;
    }

    // Always, not only for the diseases a config selects: a registry that disagrees with the tree
    // is wrong whether or not this run happens to touch the disagreeing entry (ADR 0012).
    index->validate_registry_against_tree(report);

    return Store{std::move(*index)};
}

std::filesystem::path Store::demographic_file(const std::string &section,
                                              const core::Country &country,
                                              diag::IssueReport &report) const {
    const auto &node = index_.node()["demographic"];
    const auto root_path = node.value("path", std::string{});
    if (!node.contains(section)) {
        report.error(IssueCode::data_index_invalid,
                     IssueLocation{.file = (index_.root() / "index.json").string(),
                                   .field = fmt::format("/demographic/{}", section)},
                     fmt::format("'{}' is required", section));
        return {};
    }

    const auto &child = node[section];
    const auto directory = child.value("path", std::string{});
    const auto pattern = child.value("file_name", std::string{});

    const auto name = DataIndex::substitute_named(
        pattern, {{"COUNTRY_CODE", std::to_string(country.code)}},
        index_.root() / "index.json", report);

    return index_.root() / root_path / directory / name;
}

std::optional<std::vector<core::Country>> Store::countries(diag::IssueReport &report) const {
    const auto &node = index_.node()["country"];
    const auto path = index_.root() / node.value("path", std::string{}) /
                      node.value("file_name", std::string{"countries.csv"});

    const auto document = io::read_csv(path, {}, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto column = [&](const char *name) -> std::optional<std::size_t> {
        const auto index = document->column_index(name);
        if (!index.has_value()) {
            report.error(IssueCode::csv_missing_column,
                         IssueLocation{.file = path.string(), .field = name, .line = 1U},
                         fmt::format("the countries file needs a '{}' column", name));
        }
        return index;
    };

    const auto code = column("Code");
    const auto name = column("Name");
    const auto alpha2 = column("Alpha2");
    const auto alpha3 = column("Alpha3");
    if (!code || !name || !alpha2 || !alpha3) {
        return std::nullopt;
    }

    std::vector<core::Country> result;
    result.reserve(document->num_rows());
    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto value = document->field_as_int(row, *code, report);
        if (!value.has_value()) {
            continue;
        }
        result.push_back(core::Country{.code = *value,
                                       .name = document->field(row, *name),
                                       .alpha2 = document->field(row, *alpha2),
                                       .alpha3 = document->field(row, *alpha3)});
    }

    std::sort(result.begin(), result.end(),
              [](const core::Country &left, const core::Country &right) {
                  return left.name < right.name;
              });

    return result;
}

std::optional<core::Country> Store::country(const std::string &alpha,
                                            diag::IssueReport &report) const {
    const auto all = countries(report);
    if (!all.has_value()) {
        return std::nullopt;
    }

    const auto found =
        std::find_if(all->begin(), all->end(), [&alpha](const core::Country &candidate) {
            return core::case_insensitive::equals(candidate.alpha2, alpha) ||
                   core::case_insensitive::equals(candidate.alpha3, alpha);
        });

    if (found == all->end()) {
        report.error(IssueCode::data_country_unknown,
                     IssueLocation{.field = "/inputs/settings/country_code"},
                     fmt::format("'{}' is not an ISO 3166-1 alpha-2 or alpha-3 code in this data "
                                 "store's countries file",
                                 alpha));
        return std::nullopt;
    }

    return *found;
}

std::vector<core::DiseaseInfo> Store::diseases() const {
    std::vector<core::DiseaseInfo> result;
    result.reserve(index_.registry().size());
    for (const auto &entry : index_.registry()) {
        result.push_back(
            core::DiseaseInfo{.group = entry.group, .code = entry.code, .name = entry.name});
    }

    std::sort(result.begin(), result.end(),
              [](const core::DiseaseInfo &left, const core::DiseaseInfo &right) {
                  return left.name < right.name;
              });

    return result;
}

std::optional<core::DiseaseInfo> Store::disease_info(const core::Identifier &code,
                                                     diag::IssueReport &report) const {
    const auto entry = index_.find_disease(code);
    if (!entry.has_value()) {
        // Name the nearest registered code: the upstream `pulmonar`/`pulmonary` split is exactly
        // this mistake, and the baseline's message is "Disease code: 'pulmonar' not found."
        std::string suggestion;
        for (const auto &candidate : index_.registry()) {
            const auto &text = candidate.code.to_string();
            if (text.starts_with(code.to_string()) || code.to_string().starts_with(text)) {
                suggestion = text;
                break;
            }
        }

        report.error(IssueCode::data_disease_not_in_registry,
                     IssueLocation{.field = "/running/diseases"},
                     suggestion.empty()
                         ? fmt::format("disease '{}' is not in this data store's registry",
                                       code.to_string())
                         : fmt::format("disease '{}' is not in this data store's registry; did "
                                       "you mean '{}'?",
                                       code.to_string(), suggestion));
        return std::nullopt;
    }

    return core::DiseaseInfo{.group = entry->group, .code = entry->code, .name = entry->name};
}

namespace {

/// Reads a whole CSV and maps the required column names to indices, reporting any that are absent.
struct ColumnMap {
    std::map<std::string, std::size_t> indices;
    bool complete{true};

    std::size_t at(const char *name) const { return indices.at(name); }
};

ColumnMap map_columns(const io::CsvDocument &document, const std::vector<const char *> &names,
                      diag::IssueReport &report) {
    ColumnMap result;
    for (const auto *name : names) {
        const auto index = document.column_index(name);
        if (!index.has_value()) {
            report.error(IssueCode::csv_missing_column,
                         IssueLocation{.file = document.path().string(),
                                       .field = name,
                                       .line = 1U},
                         fmt::format("required column '{}' is not in the file", name));
            result.complete = false;
            continue;
        }
        result.indices.emplace(name, *index);
    }
    return result;
}

} // namespace

std::optional<std::vector<core::PopulationItem>>
Store::population(const core::Country &country, const TimeFilter &time_filter,
                  diag::IssueReport &report) const {
    const auto path = demographic_file("population", country, report);
    const auto document = io::read_csv(path, {}, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto columns = map_columns(
        *document, {"LocID", "Time", "Age", "PopMale", "PopFemale", "PopTotal"}, report);
    if (!columns.complete) {
        return std::nullopt;
    }

    std::vector<core::PopulationItem> result;
    result.reserve(document->num_rows());
    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto time = document->field_as_int(row, columns.at("Time"), report);
        if (!time.has_value() || !time_filter(static_cast<unsigned int>(*time))) {
            continue;
        }

        result.push_back(core::PopulationItem{
            .location_id = document->field_as_int(row, columns.at("LocID"), report).value_or(0),
            .at_time = *time,
            .with_age = document->field_as_int(row, columns.at("Age"), report).value_or(0),
            .males = static_cast<float>(
                document->field_as_double(row, columns.at("PopMale"), report).value_or(0.0)),
            .females = static_cast<float>(
                document->field_as_double(row, columns.at("PopFemale"), report).value_or(0.0)),
            .total = static_cast<float>(
                document->field_as_double(row, columns.at("PopTotal"), report).value_or(0.0))});
    }

    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::vector<core::MortalityItem>>
Store::mortality(const core::Country &country, const TimeFilter &time_filter,
                 diag::IssueReport &report) const {
    const auto path = demographic_file("mortality", country, report);
    const auto document = io::read_csv(path, {}, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto columns = map_columns(
        *document, {"LocID", "Time", "Age", "DeathMale", "DeathFemale", "DeathTotal"}, report);
    if (!columns.complete) {
        return std::nullopt;
    }

    std::vector<core::MortalityItem> result;
    result.reserve(document->num_rows());
    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto time = document->field_as_int(row, columns.at("Time"), report);
        if (!time.has_value() || !time_filter(static_cast<unsigned int>(*time))) {
            continue;
        }

        result.push_back(core::MortalityItem{
            .location_id = document->field_as_int(row, columns.at("LocID"), report).value_or(0),
            .at_time = *time,
            .with_age = document->field_as_int(row, columns.at("Age"), report).value_or(0),
            .males = static_cast<float>(
                document->field_as_double(row, columns.at("DeathMale"), report).value_or(0.0)),
            .females = static_cast<float>(
                document->field_as_double(row, columns.at("DeathFemale"), report).value_or(0.0)),
            .total = static_cast<float>(
                document->field_as_double(row, columns.at("DeathTotal"), report).value_or(0.0))});
    }

    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::vector<core::BirthItem>>
Store::birth_indicators(const core::Country &country, const TimeFilter &time_filter,
                        diag::IssueReport &report) const {
    const auto path = demographic_file("indicators", country, report);
    const auto document = io::read_csv(path, {}, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto columns = map_columns(*document, {"Time", "Births", "SRB"}, report);
    if (!columns.complete) {
        return std::nullopt;
    }

    std::vector<core::BirthItem> result;
    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto time = document->field_as_int(row, columns.at("Time"), report);
        if (!time.has_value() || !time_filter(static_cast<unsigned int>(*time))) {
            continue;
        }

        result.push_back(core::BirthItem{
            .at_time = *time,
            .number = static_cast<float>(
                document->field_as_double(row, columns.at("Births"), report).value_or(0.0)),
            .sex_ratio = static_cast<float>(
                document->field_as_double(row, columns.at("SRB"), report).value_or(0.0))});
    }

    return result;
}

std::optional<std::vector<core::LifeExpectancyItem>>
Store::life_expectancy(const core::Country &country, diag::IssueReport &report) const {
    const auto path = demographic_file("indicators", country, report);
    const auto document = io::read_csv(path, {}, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto columns = map_columns(*document, {"Time", "LEx", "LExMale", "LExFemale"}, report);
    if (!columns.complete) {
        return std::nullopt;
    }

    std::vector<core::LifeExpectancyItem> result;
    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto time = document->field_as_int(row, columns.at("Time"), report);
        if (!time.has_value()) {
            continue;
        }

        result.push_back(core::LifeExpectancyItem{
            .at_time = *time,
            .both = static_cast<float>(
                document->field_as_double(row, columns.at("LEx"), report).value_or(0.0)),
            .male = static_cast<float>(
                document->field_as_double(row, columns.at("LExMale"), report).value_or(0.0)),
            .female = static_cast<float>(
                document->field_as_double(row, columns.at("LExFemale"), report).value_or(0.0))});
    }

    return result;
}

std::optional<core::DiseaseEntity> Store::disease(const core::DiseaseInfo &info,
                                                  const core::Country &country,
                                                  diag::IssueReport &report) const {
    const auto &node = index_.node()["diseases"];
    const auto &disease_node = node["disease"];

    const auto index_path = index_.root() / "index.json";
    const auto folder = DataIndex::substitute_named(
        disease_node.value("path", std::string{"{DISEASE_TYPE}"}),
        {{"DISEASE_TYPE", info.code.to_string()}}, index_path, report);
    const auto file = DataIndex::substitute_named(
        disease_node.value("file_name", std::string{}),
        {{"COUNTRY_CODE", std::to_string(country.code)}}, index_path, report);

    const auto path = index_.root() / node.value("path", std::string{}) / folder / file;
    const auto document = io::read_csv(path, {}, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto columns =
        map_columns(*document, {"age", "gender_id", "measure_id", "measure", "mean"}, report);
    if (!columns.complete) {
        return std::nullopt;
    }

    core::DiseaseEntity result;
    result.info = info;
    result.country = country;

    // std::map, not unordered: the item order this produces is part of the output, and the
    // baseline's unordered_map made it depend on the standard library's bucket layout.
    std::map<int, std::map<core::Gender, std::map<int, double>>> table;

    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto age = document->field_as_int(row, columns.at("age"), report);
        const auto gender_id = document->field_as_int(row, columns.at("gender_id"), report);
        const auto measure_id = document->field_as_int(row, columns.at("measure_id"), report);
        const auto value = document->field_as_double(row, columns.at("mean"), report);
        if (!age || !gender_id || !measure_id || !value) {
            continue;
        }

        if (*gender_id < 0 || *gender_id > 2) {
            report.error(IssueCode::csv_bad_value,
                         IssueLocation{.file = path.string(),
                                       .field = "gender_id",
                                       .line = document->line_of(row)},
                         fmt::format("'{}' is not a gender id: 1 is male, 2 is female",
                                     *gender_id));
            continue;
        }

        const auto measure_name = core::to_lower(document->field(row, columns.at("measure")));
        result.measures.try_emplace(measure_name, *measure_id);
        table[*age][static_cast<core::Gender>(*gender_id)][*measure_id] = *value;
    }

    for (const auto &[age, by_gender] : table) {
        for (const auto &[gender, measures] : by_gender) {
            result.items.push_back(
                core::DiseaseItem{.with_age = age, .gender = gender, .measures = measures});
        }
    }

    if (result.empty()) {
        report.error(IssueCode::data_missing_file, IssueLocation{.file = path.string()},
                     fmt::format("{} has no usable rows for {}", info.name, country.name));
        return std::nullopt;
    }

    return result;
}

namespace {

std::optional<core::RelativeRiskEntity> read_relative_risk_table(
    const std::filesystem::path &path, diag::IssueReport &report) {
    const auto document = io::read_csv(path, {}, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    core::RelativeRiskEntity table;
    table.columns = document->headers();
    table.rows.reserve(document->num_rows());

    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        std::vector<float> values;
        values.reserve(table.columns.size());
        for (std::size_t column = 0; column < table.columns.size(); ++column) {
            values.push_back(static_cast<float>(
                document->field_as_double(row, column, report).value_or(0.0)));
        }
        table.rows.push_back(std::move(values));
    }

    return table;
}

} // namespace

std::optional<core::RelativeRiskEntity>
Store::relative_risk_to_disease(const core::DiseaseInfo &source, const core::DiseaseInfo &target,
                                diag::IssueReport &report) const {
    const auto &node = index_.node()["diseases"];
    const auto &disease_node = node["disease"];
    const auto &risk_node = disease_node["relative_risk"];
    const auto index_path = index_.root() / "index.json";

    const auto folder = DataIndex::substitute_named(
        disease_node.value("path", std::string{"{DISEASE_TYPE}"}),
        {{"DISEASE_TYPE", source.code.to_string()}}, index_path, report);

    // The one pattern where a token name stands for two different values in sequence.
    const auto file = DataIndex::substitute_sequential(
        risk_node["to_disease"].value("file_name", std::string{}),
        {source.code.to_string(), target.code.to_string()});

    const auto path = index_.root() / node.value("path", std::string{}) / folder /
                      risk_node.value("path", std::string{}) /
                      risk_node["to_disease"].value("path", std::string{}) / file;

    if (!std::filesystem::exists(path)) {
        report.warning(IssueCode::data_missing_file, IssueLocation{.file = path.string()},
                       fmt::format("no {} to {} relative risk file; the interaction is off",
                                   source.code.to_string(), target.code.to_string()));
        return std::nullopt;
    }

    auto table = read_relative_risk_table(path, report);
    if (!table.has_value()) {
        return std::nullopt;
    }

    // A table of nothing but the default value carries no information, and the baseline skips it
    // too — reported, so the reader knows the interaction is inactive by data rather than by
    // configuration.
    const auto default_value =
        risk_node["to_disease"].value("default_value", 1.0);
    std::size_t non_default = 0;
    for (const auto &row : table->rows) {
        for (std::size_t column = 1; column < row.size(); ++column) {
            if (!core::MathHelper::equal(static_cast<double>(row[column]), default_value)) {
                ++non_default;
            }
        }
    }

    if (non_default == 0) {
        report.warning(IssueCode::data_missing_file, IssueLocation{.file = path.string()},
                       fmt::format("{} to {} relative risks are all the default value {}; the "
                                   "interaction is off",
                                   source.code.to_string(), target.code.to_string(),
                                   default_value));
        return std::nullopt;
    }

    return table;
}

std::optional<core::RelativeRiskEntity>
Store::relative_risk_to_risk_factor(const core::DiseaseInfo &source, core::Gender gender,
                                    const core::Identifier &risk_factor,
                                    diag::IssueReport &report) const {
    const auto &node = index_.node()["diseases"];
    const auto &disease_node = node["disease"];
    const auto &risk_node = disease_node["relative_risk"];
    const auto index_path = index_.root() / "index.json";

    const auto folder = DataIndex::substitute_named(
        disease_node.value("path", std::string{"{DISEASE_TYPE}"}),
        {{"DISEASE_TYPE", source.code.to_string()}}, index_path, report);

    const auto file = DataIndex::substitute_named(
        risk_node["to_risk_factor"].value("file_name", std::string{}),
        {{"GENDER", gender == core::Gender::male ? "male" : "female"},
         {"DISEASE_TYPE", source.code.to_string()},
         {"RISK_FACTOR", risk_factor.to_string()}},
        index_path, report);

    const auto path = index_.root() / node.value("path", std::string{}) / folder /
                      risk_node.value("path", std::string{}) /
                      risk_node["to_risk_factor"].value("path", std::string{}) / file;

    if (!std::filesystem::exists(path)) {
        report.warning(IssueCode::data_missing_file, IssueLocation{.file = path.string()},
                       fmt::format("no {} to {} relative risk file for {}; the effect is off",
                                   source.code.to_string(), risk_factor.to_string(),
                                   gender == core::Gender::male ? "males" : "females"));
        return std::nullopt;
    }

    return read_relative_risk_table(path, report);
}

std::optional<core::CancerParameterEntity>
Store::cancer_parameters(const core::DiseaseInfo &info, const core::Country &country,
                         diag::IssueReport &report) const {
    const auto &node = index_.node()["diseases"];
    const auto &disease_node = node["disease"];
    const auto index_path = index_.root() / "index.json";

    if (!disease_node.contains("parameters")) {
        report.error(IssueCode::data_index_invalid,
                     IssueLocation{.file = index_path.string(),
                                   .field = "/diseases/disease/parameters"},
                     "required to load cancer parameters");
        return std::nullopt;
    }

    const auto &parameters_node = disease_node["parameters"];
    const auto folder = DataIndex::substitute_named(
        disease_node.value("path", std::string{"{DISEASE_TYPE}"}),
        {{"DISEASE_TYPE", info.code.to_string()}}, index_path, report);
    const auto parameters_folder = DataIndex::substitute_named(
        parameters_node.value("path", std::string{}),
        {{"COUNTRY_CODE", std::to_string(country.code)}}, index_path, report);

    const auto directory =
        index_.root() / node.value("path", std::string{}) / folder / parameters_folder;
    if (!std::filesystem::is_directory(directory)) {
        report.error(IssueCode::data_missing_file, IssueLocation{.file = directory.string()},
                     fmt::format("{} cancer parameters for {} are missing", info.name,
                                 country.name));
        return std::nullopt;
    }

    core::CancerParameterEntity result;
    result.at_time = node.value("time_year", 0);

    for (const auto &entry : parameters_node["files"].items()) {
        const auto path = directory / entry.value().get<std::string>();
        const auto document = io::read_csv(path, {}, report);
        if (!document.has_value()) {
            continue;
        }

        const auto columns = map_columns(*document, {"Time", "Male", "Female"}, report);
        if (!columns.complete) {
            continue;
        }

        std::vector<core::LookupGenderValue> lookup;
        lookup.reserve(document->num_rows());
        for (std::size_t row = 0; row < document->num_rows(); ++row) {
            lookup.push_back(core::LookupGenderValue{
                .value = document->field_as_int(row, columns.at("Time"), report).value_or(0),
                .male = document->field_as_double(row, columns.at("Male"), report).value_or(0.0),
                .female =
                    document->field_as_double(row, columns.at("Female"), report).value_or(0.0)});
        }

        if (entry.key() == "distribution") {
            result.prevalence_distribution = std::move(lookup);
        } else if (entry.key() == "survival_rate") {
            result.survival_rate = std::move(lookup);
        } else if (entry.key() == "death_weight") {
            result.death_weight = std::move(lookup);
        } else {
            report.error(IssueCode::data_index_invalid,
                         IssueLocation{.file = index_path.string(),
                                       .field = "/diseases/disease/parameters/files"},
                         fmt::format("'{}' is not a cancer parameter file this build knows; "
                                     "expected distribution, survival_rate or death_weight",
                                     entry.key()));
        }
    }

    if (result.empty()) {
        report.error(IssueCode::data_missing_file, IssueLocation{.file = directory.string()},
                     fmt::format("{} cancer parameters for {} are incomplete", info.name,
                                 country.name));
        return std::nullopt;
    }

    return result;
}

std::optional<core::DiseaseAnalysisEntity>
Store::disease_analysis(const core::Country &country, diag::IssueReport &report) const {
    const auto &node = index_.node()["analysis"];
    const auto analysis_root = index_.root() / node.value("path", std::string{});
    const auto index_path = index_.root() / "index.json";

    core::DiseaseAnalysisEntity result;

    const auto disability_path =
        analysis_root / node.value("disability_file_name", std::string{});
    if (const auto document = io::read_csv(disability_path, {}, report)) {
        // Two columns, no header row semantics beyond the names: disease then weight.
        if (document->num_columns() < 2) {
            report.error(IssueCode::csv_missing_column,
                         IssueLocation{.file = disability_path.string(), .line = 1U},
                         "the disability weights file needs a disease column and a weight column");
        } else {
            // The header of this file is itself a data row in the upstream data, so take it too
            // when it parses as a number.
            if (const auto header_weight = document->headers()[1];
                !header_weight.empty()) {
                try {
                    const auto value = std::stof(header_weight);
                    result.disability_weights.emplace(document->headers()[0], value);
                } catch (const std::exception &) {
                    // An ordinary header; nothing to record.
                }
            }

            for (std::size_t row = 0; row < document->num_rows(); ++row) {
                const auto weight = document->field_as_double(row, 1, report);
                if (weight.has_value()) {
                    result.disability_weights.emplace(document->field(row, 0),
                                                      static_cast<float>(*weight));
                }
            }
        }
    }

    // Named "cost_of_disease" in index.json; the file it points at is an observed-YLD table.
    if (node.contains("cost_of_disease")) {
        const auto &cost_node = node["cost_of_disease"];
        const auto file = DataIndex::substitute_named(
            cost_node.value("file_name", std::string{}),
            {{"COUNTRY_CODE", std::to_string(country.code)}}, index_path, report);
        const auto path = analysis_root / cost_node.value("path", std::string{}) / file;

        if (const auto document = io::read_csv(path, {}, report)) {
            const auto columns = map_columns(*document, {"age", "gender_id", "mean"}, report);

            // The upstream cost file has no header names for these columns in every release, so
            // fall back to the fixed positions the baseline uses (6, 8, 12) when the names are
            // absent. Reported, so a reader knows which route was taken.
            if (columns.complete) {
                for (std::size_t row = 0; row < document->num_rows(); ++row) {
                    const auto age = document->field_as_int(row, columns.at("age"), report);
                    const auto gender =
                        document->field_as_int(row, columns.at("gender_id"), report);
                    const auto yld = document->field_as_double(row, columns.at("mean"), report);
                    if (age && gender && yld) {
                        result.observed_yld[*age][static_cast<core::Gender>(*gender)] = *yld;
                    }
                }
            } else if (document->num_columns() > 12) {
                report.warning(IssueCode::csv_missing_column,
                               IssueLocation{.file = path.string(), .line = 1U},
                               "using the upstream fixed column positions (7, 9 and 13) for age, "
                               "gender and the YLD mean, because the named columns are absent");
                for (std::size_t row = 0; row < document->num_rows(); ++row) {
                    const auto age = document->field_as_int(row, 6, report);
                    const auto gender = document->field_as_int(row, 8, report);
                    const auto yld = document->field_as_double(row, 12, report);
                    if (age && gender && yld) {
                        result.observed_yld[*age][static_cast<core::Gender>(*gender)] = *yld;
                    }
                }
            }
        }
    }

    if (const auto expectancy = life_expectancy(country, report)) {
        result.life_expectancy = *expectancy;
    }

    return result;
}

std::optional<std::vector<core::LmsDataRow>>
Store::lms_parameters(diag::IssueReport &report) const {
    const auto &node = index_.node()["analysis"];
    const auto path = index_.root() / node.value("path", std::string{}) /
                      node.value("lms_file_name", std::string{});

    const auto document = io::read_csv(path, {}, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    const auto columns =
        map_columns(*document, {"age", "gender_id", "lambda", "mu", "sigma"}, report);
    if (!columns.complete) {
        return std::nullopt;
    }

    std::vector<core::LmsDataRow> result;
    result.reserve(document->num_rows());
    for (std::size_t row = 0; row < document->num_rows(); ++row) {
        const auto age = document->field_as_int(row, columns.at("age"), report);
        const auto gender = document->field_as_int(row, columns.at("gender_id"), report);
        if (!age || !gender) {
            continue;
        }

        result.push_back(core::LmsDataRow{
            .age = *age,
            .gender = static_cast<core::Gender>(*gender),
            .lambda = document->field_as_double(row, columns.at("lambda"), report).value_or(0.0),
            .mu = document->field_as_double(row, columns.at("mu"), report).value_or(0.0),
            .sigma = document->field_as_double(row, columns.at("sigma"), report).value_or(0.0)});
    }

    return result;
}

} // namespace hgps::data
