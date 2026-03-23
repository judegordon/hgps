#include "ebhlm_model_parser.h"

#include "json_access.h"
#include "risk_factor_expected_loader.h"

#include "hgps_core/string.h"

#include <fmt/core.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hgps::input {

std::unique_ptr<hgps::DynamicHierarchicalLinearModelDefinition>
load_ebhlm_risk_model_definition(const nlohmann::json &opt,
                                 const Configuration &config,
                                 hgps::core::Diagnostics &diagnostics,
                                 std::string_view source_path) {
    auto expected = load_risk_factor_expected(config, diagnostics);
    if (!expected) {
        return nullptr;
    }

    auto expected_trend =
        std::make_unique<std::unordered_map<hgps::core::Identifier, double>>();
    auto trend_steps =
        std::make_unique<std::unordered_map<hgps::core::Identifier, int>>();

    LiteHierarchicalModelInfo info;

    bool ok = true;
    get_to(opt, "BoundaryPercentage", info.percentage, diagnostics, ok, source_path);
    get_to(opt, "Variables", info.variables, diagnostics, ok, source_path);
    get_to(opt, "Equations", info.equations, diagnostics, ok, source_path);
    if (!ok) {
        return nullptr;
    }

    double percentage = 0.05;
    if (info.percentage > 0.0 && info.percentage < 1.0) {
        percentage = info.percentage;
    } else {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = std::string{source_path},
                           .field_path = "BoundaryPercentage"},
                          fmt::format("Boundary percentage outside range (0, 1): {}",
                                      info.percentage));
        return nullptr;
    }

    std::map<hgps::core::Identifier, hgps::core::Identifier> variables;
    for (const auto &item : info.variables) {
        if (item.name.empty()) {
            diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                              {.source_path = std::string{source_path},
                               .field_path = "Variables"},
                              "Variable name must not be empty");
            return nullptr;
        }

        if (item.factor.empty()) {
            diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                              {.source_path = std::string{source_path},
                               .field_path = "Variables"},
                              "Variable factor must not be empty");
            return nullptr;
        }

        variables.emplace(hgps::core::Identifier{item.name},
                          hgps::core::Identifier{item.factor});

        (*expected_trend)[hgps::core::Identifier{item.factor}] = 1.0;
        (*trend_steps)[hgps::core::Identifier{item.factor}] = 0;
    }

    std::map<hgps::core::IntegerInterval, hgps::AgeGroupGenderEquation> equations;

    for (const auto &[age_key_str, gender_map] : info.equations) {
        const auto limits = hgps::core::split_string(age_key_str, "-");
        if (limits.size() != 2) {
            diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                              {.source_path = std::string{source_path},
                               .field_path = "Equations." + age_key_str},
                              fmt::format("Invalid age group '{}'", age_key_str));
            return nullptr;
        }

        int lower = 0;
        int upper = 0;
        try {
            lower = std::stoi(std::string{limits[0]});
            upper = std::stoi(std::string{limits[1]});
        } catch (const std::exception &) {
            diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                              {.source_path = std::string{source_path},
                               .field_path = "Equations." + age_key_str},
                              fmt::format("Invalid age group '{}'", age_key_str));
            return nullptr;
        }

        auto age_key = hgps::core::IntegerInterval(lower, upper);
        auto age_equations = hgps::AgeGroupGenderEquation{.age_group = age_key};

        for (const auto &[gender_key, functions] : gender_map) {
            if (hgps::core::case_insensitive::equals("male", gender_key)) {
                for (const auto &func : functions) {
                    auto function = hgps::FactorDynamicEquation{.name = func.name};
                    function.residuals_standard_deviation = func.residuals_standard_deviation;

                    for (const auto &[coeff_name, coeff_value] : func.coefficients) {
                        function.coefficients.emplace(hgps::core::to_lower(coeff_name),
                                                      coeff_value);
                    }

                    age_equations.male.emplace(hgps::core::to_lower(func.name),
                                               std::move(function));
                }
            } else if (hgps::core::case_insensitive::equals("female", gender_key)) {
                for (const auto &func : functions) {
                    auto function = hgps::FactorDynamicEquation{.name = func.name};
                    function.residuals_standard_deviation = func.residuals_standard_deviation;

                    for (const auto &[coeff_name, coeff_value] : func.coefficients) {
                        function.coefficients.emplace(hgps::core::to_lower(coeff_name),
                                                      coeff_value);
                    }

                    age_equations.female.emplace(hgps::core::to_lower(func.name),
                                                 std::move(function));
                }
            } else {
                diagnostics.error(hgps::core::DiagnosticCode::invalid_enum_value,
                                  {.source_path = std::string{source_path},
                                   .field_path =
                                       join_field_path(join_field_path("Equations", age_key_str),
                                                       gender_key)},
                                  fmt::format("Unknown model gender type: {}", gender_key));
                return nullptr;
            }
        }

        equations.emplace(age_key, std::move(age_equations));
    }

    try {
        return std::make_unique<hgps::DynamicHierarchicalLinearModelDefinition>(
            std::move(expected), std::move(expected_trend), std::move(trend_steps),
            std::move(equations), std::move(variables), percentage);
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = std::string{source_path}},
                          e.what());
        return nullptr;
    } catch (...) {
        diagnostics.error(
            hgps::core::DiagnosticCode::parse_failure,
            {.source_path = std::string{source_path}},
            "Unknown error while constructing EBHLM model definition");
        return nullptr;
    }
}

} // namespace hgps::input