#include "staticlinear_csv_loader.h"

#include "config_section_parsing.h"
#include "json_access.h"

#include "hgps_core/string_util.h"

#include <fmt/core.h>
#include <rapidcsv.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string normalise_delimiter(std::string delimiter) {
    if (delimiter == "\\t") {
        return "\t";
    }

    if (delimiter.empty()) {
        return ",";
    }

    return delimiter;
}

std::string to_lower_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<rapidcsv::Document>
open_csv_document(const std::filesystem::path &path,
                  std::string_view delimiter,
                  hgps::core::Diagnostics &diagnostics,
                  std::string_view field_path) {
    try {
        const auto normalised_delimiter = normalise_delimiter(std::string{delimiter});
        return rapidcsv::Document(path.string(), rapidcsv::LabelParams{},
                                  rapidcsv::SeparatorParams{normalised_delimiter.front()});
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                          {.source_path = path.string(), .field_path = std::string{field_path}},
                          e.what());
        return std::nullopt;
    }
}

std::optional<Eigen::MatrixXd>
load_ordered_square_matrix(const rapidcsv::Document &document,
                           std::size_t dimension,
                           const std::filesystem::path &path,
                           hgps::core::Diagnostics &diagnostics,
                           std::string_view field_path,
                           bool skip_first_column) {
    const std::size_t expected_columns = dimension + (skip_first_column ? 1U : 0U);

    if (document.GetColumnCount() != expected_columns) {
        diagnostics.error(
            hgps::core::DiagnosticCode::invalid_value,
            {.source_path = path.string(), .field_path = std::string{field_path}},
            fmt::format("Expected {} columns but found {}", expected_columns,
                        document.GetColumnCount()));
        return std::nullopt;
    }

    if (document.GetRowCount() != dimension) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = path.string(), .field_path = std::string{field_path}},
                          fmt::format("Expected {} rows but found {}", dimension,
                                      document.GetRowCount()));
        return std::nullopt;
    }

    Eigen::MatrixXd matrix{dimension, dimension};

    for (std::size_t row = 0; row < dimension; ++row) {
        for (std::size_t col = 0; col < dimension; ++col) {
            try {
                const auto csv_col = skip_first_column ? (col + 1U) : col;
                matrix(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col)) =
                    document.GetCell<double>(csv_col, row);
            } catch (const std::exception &e) {
                diagnostics.error(
                    hgps::core::DiagnosticCode::parse_failure,
                    {.source_path = path.string(), .field_path = std::string{field_path}},
                    fmt::format("Failed reading matrix value at row {}, column {}: {}", row, col,
                                e.what()));
                return std::nullopt;
            }
        }
    }

    return matrix;
}

} // namespace

namespace hgps::input {

bool is_matrix_based_risk_factor_structure(const nlohmann::json &opt) {
    return opt.contains("RiskFactorModels") && opt["RiskFactorModels"].is_object() &&
           opt["RiskFactorModels"].contains("boxcox_coefficients");
}

std::optional<MatrixCoefficientTable>
load_matrix_coefficient_table(const nlohmann::json &node,
                              const Configuration &config,
                              hgps::core::Diagnostics &diagnostics,
                              std::string_view source_path,
                              std::string_view field_path) {
    const auto file_info = get_file_info(node, config.root_path, diagnostics, source_path, field_path);
    if (!file_info.has_value()) {
        return std::nullopt;
    }

    const auto document =
        open_csv_document(file_info->name, file_info->delimiter, diagnostics, field_path);
    if (!document.has_value()) {
        return std::nullopt;
    }

    if (document->GetColumnCount() < 2) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = file_info->name.string(),
                           .field_path = std::string{field_path}},
                          "Coefficient table must have at least two columns");
        return std::nullopt;
    }

    MatrixCoefficientTable table;

    for (std::size_t row = 0; row < document->GetRowCount(); ++row) {
        std::string coefficient_name;
        try {
            coefficient_name = document->GetCell<std::string>(0, row);
        } catch (const std::exception &e) {
            diagnostics.error(hgps::core::DiagnosticCode::parse_failure,
                              {.source_path = file_info->name.string(),
                               .field_path = std::string{field_path}},
                              fmt::format("Failed reading coefficient row name at row {}: {}", row,
                                          e.what()));
            return std::nullopt;
        }

        if (hgps::core::case_insensitive::equals(coefficient_name, "EnergyIntake")) {
            coefficient_name = "log_energy_intake";
        }

        auto &row_map = table.rows[coefficient_name];

        for (std::size_t col = 1; col < document->GetColumnCount(); ++col) {
            std::string risk_factor_name;
            double coefficient_value = 0.0;

            try {
                risk_factor_name = to_lower_copy(document->GetColumnName(col));
                coefficient_value = document->GetCell<double>(col, row);
            } catch (const std::exception &e) {
                diagnostics.error(
                    hgps::core::DiagnosticCode::parse_failure,
                    {.source_path = file_info->name.string(),
                     .field_path = std::string{field_path}},
                    fmt::format("Failed reading coefficient value at row {}, column {}: {}", row,
                                col, e.what()));
                return std::nullopt;
            }

            row_map[risk_factor_name] = coefficient_value;
        }
    }

    return table;
}

std::optional<StaticLinearMatrixData>
load_staticlinear_matrix_data(const nlohmann::json &opt,
                              const Configuration &config,
                              hgps::core::Diagnostics &diagnostics,
                              std::string_view source_path) {
    StaticLinearMatrixData data;

    if (!opt.contains("RiskFactorCorrelationFile")) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "RiskFactorCorrelationFile"},
                          "Missing required key");
        return std::nullopt;
    }

    if (!opt.contains("PolicyCovarianceFile")) {
        diagnostics.error(hgps::core::DiagnosticCode::missing_key,
                          {.source_path = std::string{source_path},
                           .field_path = "PolicyCovarianceFile"},
                          "Missing required key");
        return std::nullopt;
    }

    const auto correlation_file_info =
        get_file_info(opt["RiskFactorCorrelationFile"], config.root_path, diagnostics, source_path,
                      "RiskFactorCorrelationFile");
    if (!correlation_file_info.has_value()) {
        return std::nullopt;
    }

    const auto correlation_document =
        open_csv_document(correlation_file_info->name, correlation_file_info->delimiter, diagnostics,
                          "RiskFactorCorrelationFile");
    if (!correlation_document.has_value()) {
        return std::nullopt;
    }

    const auto correlation_headers = correlation_document->GetColumnNames();
    if (correlation_headers.size() < 2) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = correlation_file_info->name.string(),
                           .field_path = "RiskFactorCorrelationFile"},
                          "Correlation matrix must include a row-label column and at least one data column");
        return std::nullopt;
    }

    data.ordered_names.reserve(correlation_headers.size() - 1U);
    for (std::size_t i = 1; i < correlation_headers.size(); ++i) {
        data.ordered_names.emplace_back(to_lower_copy(correlation_headers[i]));
    }

    const auto correlation =
        load_ordered_square_matrix(*correlation_document, data.ordered_names.size(),
                                   correlation_file_info->name, diagnostics,
                                   "RiskFactorCorrelationFile", true);
    if (!correlation.has_value()) {
        return std::nullopt;
    }
    data.correlation = std::move(*correlation);

    const auto policy_file_info =
        get_file_info(opt["PolicyCovarianceFile"], config.root_path, diagnostics, source_path,
                      "PolicyCovarianceFile");
    if (!policy_file_info.has_value()) {
        return std::nullopt;
    }

    const auto policy_document =
        open_csv_document(policy_file_info->name, policy_file_info->delimiter, diagnostics,
                          "PolicyCovarianceFile");
    if (!policy_document.has_value()) {
        return std::nullopt;
    }

    const auto policy_headers = policy_document->GetColumnNames();
    if (policy_headers.empty()) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = policy_file_info->name.string(),
                           .field_path = "PolicyCovarianceFile"},
                          "Policy covariance matrix has no columns");
        return std::nullopt;
    }

    data.policy_ordered_names.reserve(policy_headers.size());
    for (const auto &header : policy_headers) {
        data.policy_ordered_names.emplace_back(to_lower_copy(header));
    }

    if (policy_document->GetRowCount() != data.policy_ordered_names.size()) {
        diagnostics.error(hgps::core::DiagnosticCode::invalid_value,
                          {.source_path = policy_file_info->name.string(),
                           .field_path = "PolicyCovarianceFile"},
                          fmt::format("Expected {} rows but found {}", data.policy_ordered_names.size(),
                                      policy_document->GetRowCount()));
        return std::nullopt;
    }

    data.policy_column_mapping.reserve(data.ordered_names.size());
    for (const auto &ordered_name : data.ordered_names) {
        bool found = false;

        for (std::size_t index = 0; index < data.policy_ordered_names.size(); ++index) {
            if (hgps::core::case_insensitive::equals(ordered_name.to_string(),
                                                     data.policy_ordered_names[index].to_string())) {
                data.policy_column_mapping.push_back(index);
                found = true;
                break;
            }
        }

        if (!found) {
            diagnostics.error(
                hgps::core::DiagnosticCode::missing_key,
                {.source_path = policy_file_info->name.string(),
                 .field_path = "PolicyCovarianceFile"},
                fmt::format("Risk factor '{}' from correlation matrix not found in policy covariance matrix",
                            ordered_name.to_string()));
            return std::nullopt;
        }
    }

    data.policy_covariance =
        Eigen::MatrixXd{static_cast<Eigen::Index>(data.ordered_names.size()),
                        static_cast<Eigen::Index>(data.ordered_names.size())};

    for (std::size_t row = 0; row < data.ordered_names.size(); ++row) {
        for (std::size_t col = 0; col < data.ordered_names.size(); ++col) {
            try {
                const auto policy_row = data.policy_column_mapping[row];
                const auto policy_col = data.policy_column_mapping[col];

                data.policy_covariance(static_cast<Eigen::Index>(row),
                                       static_cast<Eigen::Index>(col)) =
                    policy_document->GetCell<double>(policy_col, policy_row);
            } catch (const std::exception &e) {
                diagnostics.error(
                    hgps::core::DiagnosticCode::parse_failure,
                    {.source_path = policy_file_info->name.string(),
                     .field_path = "PolicyCovarianceFile"},
                    fmt::format("Failed reading policy covariance value at row {}, column {}: {}",
                                row, col, e.what()));
                return std::nullopt;
            }
        }
    }

    return data;
}

} // namespace hgps::input