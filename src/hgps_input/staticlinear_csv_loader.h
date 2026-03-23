#pragma once

#include "config.h"
#include "hgps_core/diagnostics.h"

#include "hgps_core/identifier.h"

#include <Eigen/Dense>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hgps::input {

struct MatrixCoefficientTable {
    std::unordered_map<std::string, std::unordered_map<std::string, double>> rows;
};

struct StaticLinearMatrixData {
    std::vector<hgps::core::Identifier> ordered_names;
    Eigen::MatrixXd correlation;

    std::vector<hgps::core::Identifier> policy_ordered_names;
    std::vector<std::size_t> policy_column_mapping;
    Eigen::MatrixXd policy_covariance;
};

bool is_matrix_based_risk_factor_structure(const nlohmann::json &opt);

std::optional<MatrixCoefficientTable>
load_matrix_coefficient_table(const nlohmann::json &node,
                              const Configuration &config,
                              hgps::core::Diagnostics &diagnostics,
                              std::string_view source_path = {},
                              std::string_view field_path = {});

std::optional<StaticLinearMatrixData>
load_staticlinear_matrix_data(const nlohmann::json &opt,
                              const Configuration &config,
                              hgps::core::Diagnostics &diagnostics,
                              std::string_view source_path = {});

} // namespace hgps::input