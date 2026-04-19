// individual_id_tracking_writer.cpp
#include "individual_id_tracking_writer.h"

#include <algorithm>
#include <fmt/format.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace hgps {
namespace {

std::string csv_escape(const std::string &s) {
    if (s.find_first_of(",\"\n\r") != std::string::npos) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') {
                out += "\"\"";
            } else {
                out += c;
            }
        }
        out += '"';
        return out;
    }
    return s;
}

std::vector<std::string> extract_sorted_risk_factor_columns(const IndividualTrackingRow &row) {
    std::vector<std::string> columns;
    columns.reserve(row.risk_factors.size());

    for (const auto &[key, value] : row.risk_factors) {
        (void)value;
        columns.push_back(key);
    }

    std::sort(columns.begin(), columns.end());
    return columns;
}

} // namespace

std::filesystem::path
IndividualIDTrackingWriter::make_tracking_path(const std::filesystem::path &base_result_path) {
    auto path_str = base_result_path.string();
    const auto dot_pos = path_str.find_last_of('.');
    if (dot_pos != std::string::npos) {
        return path_str.substr(0, dot_pos) + "_IndividualIDTracking.csv";
    }
    return path_str + "_IndividualIDTracking.csv";
}

IndividualIDTrackingWriter::IndividualIDTrackingWriter(
    const std::filesystem::path &base_result_path)
    : stream_{make_tracking_path(base_result_path), std::ofstream::out | std::ofstream::trunc} {
    if (!stream_.is_open()) {
        throw std::invalid_argument(fmt::format("Cannot open IndividualIDTracking file: {}",
                                                make_tracking_path(base_result_path).string()));
    }
}

void IndividualIDTrackingWriter::write_header(const IndividualTrackingEventMessage &message) {
    if (message.rows.empty()) {
        return;
    }

    risk_factor_columns_ = extract_sorted_risk_factor_columns(message.rows.front());

    stream_ << "run,time,scenario,id,age,gender,region,ethnicity,income_category";
    for (const auto &col : risk_factor_columns_) {
        stream_ << ',' << csv_escape(col);
    }
    stream_ << '\n';
    stream_.flush();

    header_written_ = true;
}

void IndividualIDTrackingWriter::validate_columns(
    const IndividualTrackingEventMessage &message) const {
    for (const auto &row : message.rows) {
        const auto row_columns = extract_sorted_risk_factor_columns(row);
        if (row_columns != risk_factor_columns_) {
            throw std::runtime_error(
                "Individual tracking risk factor columns changed after header was written.");
        }
    }
}

void IndividualIDTrackingWriter::write_row(unsigned int run, int time, const std::string &scenario,
                                           const IndividualTrackingRow &row) {
    stream_ << run << ',' << time << ',' << csv_escape(scenario) << ',' << row.id << ',' << row.age
            << ',' << csv_escape(row.gender) << ',' << csv_escape(row.region) << ','
            << csv_escape(row.ethnicity) << ',' << csv_escape(row.income_category);

    for (const auto &col : risk_factor_columns_) {
        const auto it = row.risk_factors.find(col);
        if (it != row.risk_factors.end()) {
            stream_ << ',' << it->second;
        } else {
            stream_ << ',';
        }
    }

    stream_ << '\n';
}

void IndividualIDTrackingWriter::write(const IndividualTrackingEventMessage &message) {
    std::scoped_lock lock(mutex_);

    if (message.rows.empty()) {
        return;
    }

    if (!header_written_) {
        write_header(message);
    } else {
        validate_columns(message);
    }

    for (const auto &row : message.rows) {
        write_row(message.run_number, message.model_time, message.scenario_name, row);
    }

    ++write_count_;
    if (write_count_ % 5u == 0u) {
        stream_.flush();
    }
}

} // namespace hgps