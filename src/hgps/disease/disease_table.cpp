#include "disease_table.h"

#include <fmt/format.h>

#include <stdexcept>
#include <utility>

namespace hgps {

/* -------------------- DiseaseMeasure implementation -------------------- */

DiseaseMeasure::DiseaseMeasure(std::map<int, double> measures) : measures_{std::move(measures)} {}

std::size_t DiseaseMeasure::size() const noexcept { return measures_.size(); }

double DiseaseMeasure::at(const int measure_id) const { return measures_.at(measure_id); }

double DiseaseMeasure::operator[](const int measure_id) const { return measures_.at(measure_id); }

/* --------------------- DiseaseTable implementation --------------------- */

DiseaseTable::DiseaseTable(const core::DiseaseInfo &info, std::map<std::string, int> &&measures,
                           std::map<int, std::map<core::Gender, DiseaseMeasure>> &&data)
    : info_{info}, measures_{std::move(measures)}, data_{std::move(data)} {
    if (info_.code.is_empty()) {
        throw std::invalid_argument("Invalid disease information with empty identifier");
    }

    if (data_.empty()) {
        return;
    }

    const auto &reference_row = data_.begin()->second;
    const auto expected_measure_count = measures_.size();

    for (const auto &[age, row] : data_) {
        if (row.size() != reference_row.size()) {
            throw std::invalid_argument(
                fmt::format("Number of columns mismatch at age {} ({} vs {}).", age, row.size(),
                            reference_row.size()));
        }

        for (const auto &[gender, reference_measure] : reference_row) {
            if (!row.contains(gender)) {
                throw std::invalid_argument(
                    fmt::format("Missing gender column at age {}.", age));
            }

            if (row.at(gender).size() != expected_measure_count) {
                throw std::invalid_argument(
                    fmt::format("Number of measures mismatch at age {}.", age));
            }
        }
    }
}

const core::DiseaseInfo &DiseaseTable::info() const noexcept { return info_; }

std::size_t DiseaseTable::size() const noexcept { return rows() * cols(); }

std::size_t DiseaseTable::rows() const noexcept { return data_.size(); }

std::size_t DiseaseTable::cols() const noexcept {
    if (data_.empty()) {
        return 0;
    }

    return data_.begin()->second.size();
}

bool DiseaseTable::contains(const int age) const noexcept { return data_.contains(age); }

const std::map<std::string, int> &DiseaseTable::measures() const noexcept { return measures_; }

int DiseaseTable::at(const std::string &measure) const { return measures_.at(measure); }

int DiseaseTable::operator[](const std::string &measure) const { return measures_.at(measure); }

DiseaseMeasure &DiseaseTable::operator()(const int age, const core::Gender gender) {
    return data_.at(age).at(gender);
}

const DiseaseMeasure &DiseaseTable::operator()(const int age, const core::Gender gender) const {
    return data_.at(age).at(gender);
}

} // namespace hgps