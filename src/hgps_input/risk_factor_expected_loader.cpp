#include "risk_factor_expected_loader.h"

#include "csv_parser.h"

#include "hgps_core/exception.h"
#include "hgps_core/string_util.h"

#include <fmt/core.h>

namespace hgps::input {

std::unique_ptr<hgps::RiskFactorSexAgeTable>
load_risk_factor_expected(const Configuration &config,
                          hgps::core::Diagnostics &diagnostics) {
    const auto &info = config.modelling.baseline_adjustment;

    if (!hgps::core::case_insensitive::equals(info.format, "CSV")) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.field_path = "modelling.baseline_adjustment.format"},
                          "Unsupported baseline adjustment file format: " + info.format);
        return nullptr;
    }

    if (!info.file_names.contains("factorsmean_male")) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.field_path = "modelling.baseline_adjustment.file_names.factorsmean_male"},
                          "Missing required baseline adjustment file");
        return nullptr;
    }

    if (!info.file_names.contains("factorsmean_female")) {
        diagnostics.error(
            hgps::core::DiagnosticCode::missing_key,
            {.field_path = "modelling.baseline_adjustment.file_names.factorsmean_female"},
            "Missing required baseline adjustment file");
        return nullptr;
    }

    auto table = std::make_unique<hgps::RiskFactorSexAgeTable>();

    const auto male_filename = info.file_names.at("factorsmean_male").string();
    const auto female_filename = info.file_names.at("factorsmean_female").string();

    try {
        table->emplace_row(hgps::core::Gender::male,
                           load_baseline_from_csv(male_filename, info.delimiter));
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = male_filename,
                           .field_path = "modelling.baseline_adjustment.file_names.factorsmean_male"},
                          fmt::format("Failed to parse male baseline adjustment file: {}", e.what()));
        return nullptr;
    }

    try {
        table->emplace_row(hgps::core::Gender::female,
                           load_baseline_from_csv(female_filename, info.delimiter));
    } catch (const std::exception &e) {
        diagnostics.error(
            hgps::core::DiagnosticCode::parse_failure,
            {.source_path = female_filename,
             .field_path = "modelling.baseline_adjustment.file_names.factorsmean_female"},
            fmt::format("Failed to parse female baseline adjustment file: {}", e.what()));
        return nullptr;
    }

    const auto max_age = static_cast<std::size_t>(config.settings.age_range.upper());
    for (const auto &[gender, factors] : *table) {
        for (const auto &[factor_name, values] : factors) {
            if (values.size() <= max_age) {
                diagnostics.error(
                    hgps::core::DiagnosticCode::invalid_value,
                    {.field_path = "modelling.baseline_adjustment"},
                    fmt::format(
                        "Baseline adjustment file for '{}' does not cover required age range [{}].",
                        factor_name.to_string(),
                        config.settings.age_range.to_string()));
                return nullptr;
            }
        }
    }

    return table;
}

} // namespace hgps::input