#include "model_repository_registration.h"

#include "model_parser.h"
#include "model_parser_common.h"

#include "config_section_parsing.h"
#include "csvparser.h"
#include "hgps/repository.h"

#include <fmt/core.h>

#include <any>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

bool parse_model_type(const std::string &model_type_str,
                      hgps::RiskFactorModelType &model_type,
                      hgps::core::Diagnostics &diagnostics) {
    if (model_type_str == "static") {
        model_type = hgps::RiskFactorModelType::Static;
        return true;
    }

    if (model_type_str == "dynamic") {
        model_type = hgps::RiskFactorModelType::Dynamic;
        return true;
    }

    diagnostics.error(hgps::core::DiagnosticCode::invalid_enum_value,
                      {.field_path = "modelling.risk_factor_models"},
                      fmt::format("Unknown risk factor model type '{}'", model_type_str));
    return false;
}

bool load_and_register_region_prevalence(hgps::CachedRepository &repository,
                                         const nlohmann::json &opt,
                                         const hgps::input::Configuration &config,
                                         hgps::core::Diagnostics &diagnostics,
                                         std::string_view source_path) {
    if (!opt.contains("RegionFile")) {
        return true;
    }

    const auto region_file =
        hgps::input::get_file_info(opt["RegionFile"], config.root_path, diagnostics, source_path,
                                   "RegionFile");
    if (!region_file.has_value()) {
        return false;
    }

    hgps::core::DataTable region_table;
    try {
        region_table = hgps::input::load_datatable_from_csv(*region_file);
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = region_file->name.string(), .field_path = "RegionFile"},
                          e.what());
        return false;
    }

    std::map<hgps::core::Identifier,
             std::map<hgps::core::Gender, std::map<std::string, double>>>
        region_data;

    std::vector<std::string> region_columns;
    for (std::size_t col = 0; col < region_table.num_columns(); ++col) {
        const std::string col_name = region_table.column(col).name();
        if (col_name.starts_with("region")) {
            region_columns.push_back(col_name);
        }
    }

    try {
        for (std::size_t row = 0; row < region_table.num_rows(); ++row) {
            const int age = std::any_cast<int>(region_table.column("Age").value(row));
            const int gender_int = std::any_cast<int>(region_table.column("Gender").value(row));
            const auto gender =
                (gender_int == 1) ? hgps::core::Gender::male : hgps::core::Gender::female;

            const hgps::core::Identifier age_id{"age_" + std::to_string(age)};

            for (const auto &region_col : region_columns) {
                const auto probability =
                    std::any_cast<double>(region_table.column(region_col).value(row));
                region_data[age_id][gender][region_col] = probability;
            }
        }
    } catch (const std::bad_any_cast &) {
        diagnostics.error(hgps::core::DiagnosticCode::wrong_type,
                          {.source_path = region_file->name.string(), .field_path = "RegionFile"},
                          "Region file contains values of unexpected type");
        return false;
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = region_file->name.string(), .field_path = "RegionFile"},
                          e.what());
        return false;
    }

    repository.register_region_prevalence(region_data);
    return true;
}

bool load_and_register_ethnicity_prevalence(hgps::CachedRepository &repository,
                                            const nlohmann::json &opt,
                                            const hgps::input::Configuration &config,
                                            hgps::core::Diagnostics &diagnostics,
                                            std::string_view source_path) {
    if (!opt.contains("EthnicityFile")) {
        return true;
    }

    const auto ethnicity_file =
        hgps::input::get_file_info(opt["EthnicityFile"], config.root_path, diagnostics, source_path,
                                   "EthnicityFile");
    if (!ethnicity_file.has_value()) {
        return false;
    }

    hgps::core::DataTable ethnicity_table;
    try {
        ethnicity_table = hgps::input::load_datatable_from_csv(*ethnicity_file);
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = ethnicity_file->name.string(),
                           .field_path = "EthnicityFile"},
                          e.what());
        return false;
    }

    std::map<hgps::core::Identifier,
             std::map<hgps::core::Gender,
                      std::map<std::string, std::map<std::string, double>>>>
        ethnicity_data;

    std::vector<std::string> region_columns;
    for (std::size_t col = 0; col < ethnicity_table.num_columns(); ++col) {
        const std::string col_name = ethnicity_table.column(col).name();
        if (col_name.starts_with("region")) {
            region_columns.push_back(col_name);
        }
    }

    try {
        for (std::size_t row = 0; row < ethnicity_table.num_rows(); ++row) {
            const int adult = std::any_cast<int>(ethnicity_table.column("adult").value(row));
            const int gender_int = std::any_cast<int>(ethnicity_table.column("gender").value(row));
            const int ethnicity_int =
                std::any_cast<int>(ethnicity_table.column("ethnicity").value(row));

            const auto age_group =
                (adult == 0) ? hgps::core::Identifier{"Under18"} : hgps::core::Identifier{"Over18"};
            const auto gender =
                (gender_int == 1) ? hgps::core::Gender::male : hgps::core::Gender::female;
            const std::string ethnicity_name = std::to_string(ethnicity_int);

            for (const auto &region_col : region_columns) {
                const auto probability =
                    std::any_cast<double>(ethnicity_table.column(region_col).value(row));
                ethnicity_data[age_group][gender][region_col][ethnicity_name] = probability;
            }
        }
    } catch (const std::bad_any_cast &) {
        diagnostics.error(hgps::core::DiagnosticCode::wrong_type,
                          {.source_path = ethnicity_file->name.string(),
                           .field_path = "EthnicityFile"},
                          "Ethnicity file contains values of unexpected type");
        return false;
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = ethnicity_file->name.string(),
                           .field_path = "EthnicityFile"},
                          e.what());
        return false;
    }

    repository.register_ethnicity_prevalence(ethnicity_data);
    return true;
}

} // namespace

namespace hgps::input {

void register_risk_factor_model_definitions(hgps::CachedRepository &repository,
                                            const Configuration &config,
                                            hgps::core::Diagnostics &diagnostics) {
    for (const auto &[model_type_str, model_path] : config.modelling.risk_factor_models) {
        hgps::RiskFactorModelType model_type{};
        if (!parse_model_type(model_type_str, model_type, diagnostics)) {
            continue;
        }

        auto model_definition =
            load_risk_model_definition(model_type, model_path, config, diagnostics);
        if (!model_definition) {
            continue;
        }

        repository.register_risk_factor_model_definition(model_type, std::move(model_definition));
    }

    for (const auto &[model_type_str, model_path] : config.modelling.risk_factor_models) {
        if (model_type_str != "static") {
            continue;
        }

        const auto loaded_model = load_and_validate_model_json(model_path, diagnostics);
        if (!loaded_model.has_value()) {
            return;
        }

        const auto &[model_name, opt] = *loaded_model;
        (void)model_name;

        if (!load_and_register_region_prevalence(repository, opt, config, diagnostics,
                                                 model_path.string())) {
            return;
        }

        if (!load_and_register_ethnicity_prevalence(repository, opt, config, diagnostics,
                                                    model_path.string())) {
            return;
        }

        break;
    }
}

} // namespace hgps::input