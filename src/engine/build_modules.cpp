#include "build_modules.h"

#include "core/string_util.h"
#include "io/csv_reader.h"
#include "model/predictor_resolver.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <fmt/format.h>

namespace hgps::engine {
namespace {

/// @brief The union of what two models assign: either one giving people an attribute is enough.
model::AssignedAttributes merge(model::AssignedAttributes left,
                                const model::AssignedAttributes &right) {
    left.income_category |= right.income_category;
    left.income |= right.income;
    left.physical_activity |= right.physical_activity;
    left.region |= right.region;
    left.ethnicity |= right.ethnicity;
    left.sector |= right.sector;
    return left;
}

using diag::IssueCode;
using diag::IssueLocation;

core::Gender to_gender(const std::string &name) {
    if (core::case_insensitive::equals(name, "male")) {
        return core::Gender::male;
    }
    if (core::case_insensitive::equals(name, "female")) {
        return core::Gender::female;
    }
    return core::Gender::unknown;
}

model::DiseaseTable to_disease_table(const core::DiseaseEntity &entity) {
    std::map<int, std::map<core::Gender, model::DiseaseMeasure>> data;
    for (const auto &item : entity.items) {
        data[item.with_age][item.gender] = model::DiseaseMeasure{item.measures};
    }

    return model::DiseaseTable{entity.info, entity.measures, std::move(data)};
}

/// A disease-to-disease relative risk table: age rows, one column per sex.
std::optional<model::FloatAgeGenderTable>
to_relative_risk_table(const core::RelativeRiskEntity &entity, const std::string &source,
                       diag::IssueReport &report) {
    if (entity.rows.empty() || entity.columns.size() < 2) {
        report.error(IssueCode::data_index_invalid, IssueLocation{.file = source},
                     "a relative risk table needs an age column and at least one sex column");
        return std::nullopt;
    }

    std::vector<core::Gender> columns;
    for (std::size_t i = 1; i < entity.columns.size(); ++i) {
        const auto gender = to_gender(entity.columns[i]);
        if (gender == core::Gender::unknown) {
            report.error(IssueCode::csv_bad_value,
                         IssueLocation{.file = source, .field = entity.columns[i], .line = 1U},
                         fmt::format("'{}' is not a sex column name", entity.columns[i]));
            return std::nullopt;
        }
        columns.push_back(gender);
    }

    std::vector<int> rows;
    rows.reserve(entity.rows.size());
    core::FloatArray2D data{entity.rows.size(), columns.size()};

    for (std::size_t i = 0; i < entity.rows.size(); ++i) {
        const auto &row = entity.rows[i];
        rows.push_back(static_cast<int>(row.at(0)));
        for (std::size_t j = 1; j < row.size() && j <= columns.size(); ++j) {
            data(i, j - 1) = row[j];
        }
    }

    try {
        return model::FloatAgeGenderTable{model::MonotonicVector<int>{rows}, columns,
                                          std::move(data)};
    } catch (const std::exception &error) {
        report.error(IssueCode::csv_bad_value, IssueLocation{.file = source},
                     fmt::format("the age column is not strictly increasing: {}", error.what()));
        return std::nullopt;
    }
}

/// A risk-factor relative risk table: age rows, one column per factor value band.
std::optional<model::RelativeRiskLookup>
to_relative_risk_lookup(const core::RelativeRiskEntity &entity, const std::string &source,
                        diag::IssueReport &report) {
    if (entity.rows.empty() || entity.columns.size() < 2) {
        report.error(IssueCode::data_index_invalid, IssueLocation{.file = source},
                     "a relative risk lookup needs an age column and at least one value column");
        return std::nullopt;
    }

    std::vector<float> value_columns;
    for (std::size_t i = 1; i < entity.columns.size(); ++i) {
        try {
            value_columns.push_back(std::stof(entity.columns[i]));
        } catch (const std::exception &) {
            report.error(IssueCode::csv_bad_value,
                         IssueLocation{.file = source, .field = entity.columns[i], .line = 1U},
                         fmt::format("'{}' is not a risk factor value", entity.columns[i]));
            return std::nullopt;
        }
    }

    std::vector<int> rows;
    rows.reserve(entity.rows.size());
    core::FloatArray2D data{entity.rows.size(), value_columns.size()};

    for (std::size_t i = 0; i < entity.rows.size(); ++i) {
        const auto &row = entity.rows[i];
        rows.push_back(static_cast<int>(row.at(0)));
        for (std::size_t j = 1; j < row.size() && j <= value_columns.size(); ++j) {
            data(i, j - 1) = row[j];
        }
    }

    try {
        return model::RelativeRiskLookup{model::MonotonicVector<int>{rows},
                                         model::MonotonicVector<float>{value_columns},
                                         std::move(data)};
    } catch (const std::exception &error) {
        report.error(IssueCode::csv_bad_value, IssueLocation{.file = source},
                     fmt::format("the table's breakpoints are not strictly monotonic: {}",
                                 error.what()));
        return std::nullopt;
    }
}

model::DiseaseParameter to_disease_parameter(const core::CancerParameterEntity &entity) {
    model::ParameterLookup distribution;
    for (const auto &item : entity.prevalence_distribution) {
        distribution.emplace(item.value, model::DoubleGenderValue{item.male, item.female});
    }

    model::ParameterLookup survival;
    for (const auto &item : entity.survival_rate) {
        survival.emplace(item.value, model::DoubleGenderValue{item.male, item.female});
    }

    // The death weights are indexed by time since onset, so the table is shifted to start at
    // zero — as upstream, whose comment says the same.
    model::ParameterLookup deaths;
    const auto offset = entity.death_weight.empty() ? 0 : entity.death_weight.front().value;
    for (const auto &item : entity.death_weight) {
        deaths.emplace(item.value - offset, model::DoubleGenderValue{item.male, item.female});
    }

    return model::DiseaseParameter{entity.at_time, std::move(distribution), std::move(survival),
                                   std::move(deaths)};
}

model::LmsDefinition to_lms_definition(const std::vector<core::LmsDataRow> &rows) {
    model::LmsDataset dataset;
    for (const auto &row : rows) {
        dataset[static_cast<unsigned int>(row.age)][row.gender] =
            model::LmsRecord{.lambda = row.lambda, .mu = row.mu, .sigma = row.sigma};
    }
    return model::LmsDefinition{std::move(dataset)};
}

std::optional<model::AnalysisDefinition>
to_analysis_definition(const core::DiseaseAnalysisEntity &entity, diag::IssueReport &report) {
    if (entity.life_expectancy.empty()) {
        report.error(IssueCode::data_missing_file, IssueLocation{},
                     "the data store has no life expectancy series, so no burden of disease can "
                     "be computed");
        return std::nullopt;
    }

    std::vector<int> years;
    years.reserve(entity.life_expectancy.size());
    core::FloatArray2D life_expectancy{entity.life_expectancy.size(), 2};
    for (std::size_t i = 0; i < entity.life_expectancy.size(); ++i) {
        const auto &item = entity.life_expectancy[i];
        years.push_back(item.at_time);
        life_expectancy(i, 0) = item.male;
        life_expectancy(i, 1) = item.female;
    }

    const std::vector<core::Gender> columns{core::Gender::male, core::Gender::female};

    if (entity.observed_yld.empty()) {
        report.error(IssueCode::data_missing_file, IssueLocation{},
                     "the data store has no observed YLD table");
        return std::nullopt;
    }

    // The observed YLD the residual disability weight is calibrated against.
    const auto min_age = entity.observed_yld.begin()->first;
    const auto max_age = entity.observed_yld.rbegin()->first;
    auto observed =
        model::create_age_gender_table<double>(core::IntegerInterval{min_age, max_age});
    for (const auto &[age, by_gender] : entity.observed_yld) {
        for (const auto gender : columns) {
            const auto found = by_gender.find(gender);
            if (found != by_gender.end()) {
                observed.at(age, gender) = found->second;
            }
        }
    }

    std::map<core::Identifier, float> weights;
    for (const auto &[disease, weight] : entity.disability_weights) {
        try {
            weights.emplace(core::Identifier{disease}, weight);
        } catch (const std::invalid_argument &) {
            // A weight row whose name is not a valid identifier is not a disease this model can
            // attribute disability to; named rather than silently dropped.
            report.warning(IssueCode::csv_bad_value, IssueLocation{.field = disease},
                           "not a valid disease code, so its disability weight is unused");
        }
    }

    try {
        return model::AnalysisDefinition{
            model::GenderTable<int, float>{model::MonotonicVector<int>{years}, columns,
                                           std::move(life_expectancy)},
            std::move(observed), std::move(weights)};
    } catch (const std::exception &error) {
        report.error(IssueCode::data_index_invalid, IssueLocation{},
                     fmt::format("the life expectancy series is not usable: {}", error.what()));
        return std::nullopt;
    }
}

} // namespace

std::optional<LoadedInputs> load_inputs(const config::Config &config, const data::Store &store,
                                        diag::IssueReport &report) {
    const auto before = report.error_count();
    LoadedInputs loaded;

    // 1. The country.
    const auto country = store.country(config.settings.country_code, report);
    if (!country.has_value()) {
        return std::nullopt;
    }

    // 2. The selected diseases, in the order the config lists them.
    std::vector<core::DiseaseInfo> diseases;
    for (const auto &code : config.running.diseases) {
        core::Identifier identifier;
        try {
            identifier = core::Identifier{code};
        } catch (const std::invalid_argument &error) {
            report.error(IssueCode::config_bad_value,
                         IssueLocation{.field = "/running/diseases"},
                         fmt::format("'{}' is not a valid disease code: {}", code, error.what()));
            continue;
        }

        const auto info = store.disease_info(identifier, report);
        if (info.has_value()) {
            diseases.push_back(*info);
        }
    }

    // 3. The input dataset.
    io::CsvOptions dataset_options;
    if (!config.dataset.delimiter.empty()) {
        dataset_options.delimiter = config.dataset.delimiter.front();
    }

    auto table = io::load_datatable_from_csv(config.dataset.name, config.dataset.columns,
                                             dataset_options, report);
    if (!table.has_value()) {
        return std::nullopt;
    }

    // 4. The risk factor mapping, in declaration order.
    std::vector<model::MappingEntry> mapping_entries;
    mapping_entries.reserve(config.modelling.risk_factors.size());
    for (const auto &factor : config.modelling.risk_factors) {
        mapping_entries.emplace_back(factor.name, factor.level, factor.range);
    }
    model::HierarchicalMapping mapping{std::move(mapping_entries)};

    // 5. The population series and life table.
    const auto start = config.running.start_time;
    const auto stop = config.running.stop_time;
    const auto in_horizon = [start, stop](unsigned int year) {
        return year >= start && year <= stop;
    };

    const auto population = store.population(*country, in_horizon, report);
    const auto births = store.birth_indicators(*country, in_horizon, report);
    const auto deaths = store.mortality(*country, in_horizon, report);
    if (!population || !births || !deaths) {
        return std::nullopt;
    }

    for (const auto &item : *population) {
        loaded.population_data[item.at_time].emplace(
            item.with_age, model::PopulationRecord{item.with_age, item.males, item.females});
    }

    // The configured age range has to cover every age the population data carries, because the
    // initial cohort is drawn from that data while several per-age tables — the disease models'
    // average relative risk, the migration target, the analysis — are built over the configured
    // range. A person the data supplies and the range does not cover indexes those tables with a
    // key they do not have.
    //
    // Both implementations assume this and neither checked it. The baseline reaches the same
    // situation inside a parallel loop; this build reached it as `map::at: key not found` with no
    // location, which is how the second synthetic pack found it. Narrowing the range to mean
    // "simulate only these ages" would be a modelling change — which ages receive births, deaths
    // and migration — so this refuses rather than inventing one.
    if (!loaded.population_data.empty()) {
        int lowest = std::numeric_limits<int>::max();
        int highest = std::numeric_limits<int>::min();
        for (const auto &[year, by_age] : loaded.population_data) {
            if (by_age.empty()) {
                continue;
            }
            lowest = std::min(lowest, by_age.begin()->first);
            highest = std::max(highest, by_age.rbegin()->first);
        }

        const auto &configured = config.settings.age_range;
        if (lowest <= highest && (lowest < configured.lower() || highest > configured.upper())) {
            report.error(IssueCode::config_bad_value,
                         IssueLocation{.field = "/inputs/settings/age_range"},
                         fmt::format("[{}, {}] does not cover the ages the population data "
                                     "carries, [{}, {}]; the cohort is drawn from that data, so a "
                                     "person outside the range has no row in the per-age tables "
                                     "the run needs",
                                     configured.lower(), configured.upper(), lowest, highest));
            return std::nullopt;
        }
    }

    std::map<int, model::Birth> birth_table;
    for (const auto &item : *births) {
        birth_table.emplace(item.at_time, model::Birth{item.number, item.sex_ratio});
    }

    std::map<int, std::map<int, model::Mortality>> death_table;
    for (const auto &item : *deaths) {
        death_table[item.at_time].emplace(item.with_age,
                                          model::Mortality{item.males, item.females});
    }

    loaded.life_table = model::LifeTable{std::move(birth_table), std::move(death_table)};

    // 6. The LMS growth reference and the burden-of-disease inputs.
    const auto lms_rows = store.lms_parameters(report);
    if (!lms_rows.has_value() || lms_rows->empty()) {
        report.error(IssueCode::data_missing_file, IssueLocation{},
                     "the data store has no LMS parameters, so weight cannot be classified");
        return std::nullopt;
    }
    loaded.lms = to_lms_definition(*lms_rows);

    const auto analysis_entity = store.disease_analysis(*country, report);
    if (!analysis_entity.has_value()) {
        return std::nullopt;
    }
    loaded.analysis = to_analysis_definition(*analysis_entity, report);

    // 7. The disease definitions: every measure table and relative risk, loaded now.
    std::vector<model::MappingEntry> risk_factors;
    for (int level = 1; level <= mapping.max_level(); ++level) {
        const auto at_level = mapping.at_level(level);
        risk_factors.insert(risk_factors.end(), at_level.begin(), at_level.end());
    }

    for (const auto &info : diseases) {
        const auto entity = store.disease(info, *country, report);
        if (!entity.has_value()) {
            continue;
        }

        model::RelativeRiskTableMap to_diseases;
        for (const auto &other : diseases) {
            if (other.code == info.code) {
                continue;
            }
            const auto table_entity = store.relative_risk_to_disease(info, other, report);
            if (!table_entity.has_value()) {
                continue;
            }
            const auto converted = to_relative_risk_table(
                *table_entity, fmt::format("{} to {} relative risk", info.code.to_string(),
                                           other.code.to_string()),
                report);
            if (converted.has_value()) {
                to_diseases.emplace(other.code, *converted);
            }
        }

        model::RelativeRiskLookupMap to_factors;
        for (const auto &factor : risk_factors) {
            for (const auto gender : {core::Gender::male, core::Gender::female}) {
                const auto lookup_entity =
                    store.relative_risk_to_risk_factor(info, gender, factor.key(), report);
                if (!lookup_entity.has_value()) {
                    continue;
                }
                const auto converted = to_relative_risk_lookup(
                    *lookup_entity,
                    fmt::format("{} to {} relative risk", info.code.to_string(),
                                factor.key().to_string()),
                    report);
                if (converted.has_value()) {
                    to_factors[factor.key()].emplace(gender, *converted);
                }
            }
        }

        model::DiseaseParameter parameter;
        if (info.group == core::DiseaseGroup::cancer) {
            const auto cancer = store.cancer_parameters(info, *country, report);
            if (!cancer.has_value()) {
                continue;
            }
            parameter = to_disease_parameter(*cancer);
        }

        model::DiseaseDefinition definition{to_disease_table(*entity), std::move(to_diseases),
                                            std::move(to_factors), std::move(parameter)};

        // The population impact fraction, when the run asks for one. Loaded here, for every selected
        // disease, before any worker thread exists — as everything else is, and unlike upstream, whose
        // PIF arrives through the lazily populated repository that is audit finding B-02
        // (ADR 0038).
        //
        // A disease the pack has no fractions for is an error and not a shrug: a run that was asked
        // for a policy and applied none, and then reported success, is indistinguishable from a run of
        // the baseline. The store's message names what it does have.
        if (config.population_impact_fraction.enabled) {
            const auto rows = store.population_impact_fraction(
                info, *country, config.population_impact_fraction.risk_factor,
                config.population_impact_fraction.scenario, report);
            if (!rows.has_value()) {
                continue;
            }

            const auto path = fmt::format("{} population impact fractions for {}/{}",
                                          info.code.to_string(),
                                          config.population_impact_fraction.risk_factor,
                                          config.population_impact_fraction.scenario);
            auto pif = model::PifTable::build(*rows, path, report);
            if (!pif.has_value()) {
                continue;
            }

            // A table of nothing but zeros is a policy that does nothing, which is a legitimate
            // scenario and also a plausible mistake, so it is said out loud rather than run silently.
            if (pif->largest() == 0.0) {
                report.warning(IssueCode::data_index_invalid,
                               IssueLocation{.file = path},
                               fmt::format("every population impact fraction for {} is zero, so the "
                                           "policy has no effect on this disease",
                                           info.code.to_string()));
            }

            definition.set_population_impact_fraction(std::move(*pif));
        }

        loaded.diseases.emplace(info.code, std::move(definition));
    }

    // 8. The FactorsMean tables and the model definitions.
    loaded.expected = config::models::load_expected_values(config, report);
    if (!loaded.expected) {
        return std::nullopt;
    }

    // 9. The cohort size and the immutable model input.
    const auto real_population =
        loaded.population_data.contains(static_cast<int>(start))
            ? [&loaded, start]() {
                  double total = 0.0;
                  for (const auto &[age, record] :
                       loaded.population_data.at(static_cast<int>(start))) {
                      total += static_cast<double>(record.total());
                  }
                  return total;
              }()
            : 0.0;

    loaded.cohort_size =
        static_cast<std::size_t>(config.settings.size_fraction * real_population);
    if (loaded.cohort_size == 0) {
        report.error(IssueCode::config_bad_value,
                     IssueLocation{.field = "/inputs/settings/size_fraction"},
                     fmt::format("{} of the {} population of {:.0f} rounds to no people at all",
                                 config.settings.size_fraction, start, real_population));
        return std::nullopt;
    }

    model::Settings settings{.country = *country,
                             .size_fraction = config.settings.size_fraction,
                             .age_range = config.settings.age_range};

    model::RunInfo run_info{.start_time = config.running.start_time,
                            .stop_time = config.running.stop_time,
                            .seed = config.running.seed,
                            .verbosity = config.verbosity,
                            .comorbidities = config.output.comorbidities,
                            .policy_start_year = config.modelling.policy_start_year,
                            .trial_runs = config.running.trial_runs};

    model::SesDefinition ses{.function_name = config.modelling.ses_model.function_name,
                             .parameters = config.modelling.ses_model.function_parameters};

    loaded.inputs = std::make_shared<const model::ModelInput>(
        std::move(*table), std::move(settings), run_info, std::move(ses), std::move(mapping),
        std::move(diseases), config.project_requirements, config.output.individual_id_tracking);

    if (report.error_count() != before) {
        return std::nullopt;
    }

    return loaded;
}

std::optional<sim::Modules> build_modules(const LoadedInputs &loaded,
                                          const config::Config &config,
                                          sim::ScenarioJournal &journal,
                                          diag::IssueReport &report) {
    const auto before = report.error_count();

    sim::Modules modules;

    auto demographic = std::make_unique<model::DemographicModule>(loaded.population_data,
                                                                   *loaded.life_table);

    modules.ses = std::make_unique<model::SesNoiseModule>(
        loaded.inputs->ses_definition().function_name,
        loaded.inputs->ses_definition().parameters);

    // One model instance per scenario: they hold per-run state, and sharing it would couple the
    // two futures through something other than the journal.
    const config::models::LoadContext context{.mapping = &loaded.inputs->risk_mapping(),
                                               .expected = loaded.expected,
                                               .config = &config,
                                               .trend = loaded.trend,
                                               .trend_steps = loaded.trend_steps,
                                               // Filled in by the loader once the static model has
                                               // said what it generates; empty is correct here.
                                               .extra_factors = {}};

    auto models = config::models::load_risk_factor_models(context, report);
    if (!models.has_value()) {
        return std::nullopt;
    }

    // Region and ethnicity come out of the static model's own file but are assigned by the
    // demographic module, which is the only thing that sees a person before their risk factors
    // exist.
    const bool assigns_region = !models->prevalence.region.empty();
    const bool assigns_ethnicity = !models->prevalence.ethnicity.empty();
    if (assigns_region) {
        demographic->set_region_prevalence(std::move(models->prevalence.region));
    }
    if (assigns_ethnicity) {
        demographic->set_ethnicity_prevalence(std::move(models->prevalence.ethnicity));
    }

    modules.demographic = std::move(demographic);

    // What the models assign decides part of the output's column set, so it is read before the
    // models are handed to the host module.
    auto assigned = merge(models->static_model->assigns(), models->dynamic_model->assigns());

    // Region and ethnicity are the two attributes no *model* claims: `StaticLinearModel::assigns`
    // leaves both false and says so, because it is the demographic module that gives them to a
    // person. Nothing then set them, so `AssignedAttributes::region` and `::ethnicity` were always
    // false and the two branches in `initialise_output_channels` that read them were dead — the
    // `mean_region` column of the FINCH and India outputs is there because their configs declare
    // `Region` as a level-0 risk factor and the mapping loop adds it, not because of the branch
    // meant to decide it. Set here, from the prevalence data that actually makes the assignment
    // possible, so the condition says what it means. It adds no column to any existing example:
    // each one that has the data also names the factor in its mapping, and `add` is
    // case-insensitively deduplicated.
    assigned.region |= assigns_region;
    assigned.ethnicity |= assigns_ethnicity;

    modules.risk_factor = std::make_unique<model::RiskFactorHostModule>(
        std::move(models->static_model), std::move(models->dynamic_model), journal);

    std::map<core::Identifier, std::unique_ptr<model::DiseaseModel>> disease_models;
    for (const auto &info : loaded.inputs->diseases()) {
        const auto definition = loaded.diseases.find(info.code);
        if (definition == loaded.diseases.end()) {
            report.error(IssueCode::data_missing_file, IssueLocation{.field = "/running/diseases"},
                         fmt::format("no definition was loaded for disease '{}'",
                                     info.code.to_string()));
            continue;
        }

        disease_models.emplace(info.code,
                               model::create_disease_model(definition->second,
                                                            model::WeightModel{loaded.lms},
                                                            loaded.inputs->settings().age_range));
    }

    modules.disease = std::make_unique<model::DiseaseModule>(std::move(disease_models));

    modules.analysis = std::make_unique<model::AnalysisModule>(
        *loaded.analysis, model::WeightModel{loaded.lms}, loaded.inputs->settings().age_range,
        loaded.inputs->run().comorbidities);
    modules.analysis->set_income_analysis_enabled(loaded.inputs->income_analysis_enabled());
    modules.analysis->set_assigned_attributes(assigned);

    if (report.error_count() != before) {
        return std::nullopt;
    }

    return modules;
}

} // namespace hgps::engine
