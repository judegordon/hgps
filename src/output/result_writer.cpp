#include "result_writer.h"

#include "core/string_util.h"
#include "diagnostics/internal_error.h"

#include <array>
#include <chrono>
#include <ctime>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace hgps::output {
namespace {

/// A fixed, locale-independent rendering for every number in the CSV.
///
/// {:.10g} keeps ten significant digits, which is enough to distinguish any two values the model
/// produces and short enough to keep the files readable. Using std::format rather than an
/// ostream also means the output cannot be changed by another part of the program setting a
/// stream flag.
std::string format_value(double value) { return fmt::format("{:.10g}", value); }

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
    ::gmtime_r(&now, &parts);

    std::array<char, 32> buffer{};
    const auto written = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &parts);
    return std::string{buffer.data(), written};
}

nlohmann::json to_json(const model::ResultByGender &value) {
    return nlohmann::json{{"male", value.male}, {"female", value.female}};
}

} // namespace

ResultWriter::ResultWriter(std::filesystem::path base_path, RunMetadata metadata,
                           bool write_income_files, core::IncomeCategoryLayout income_layout)
    : metadata_{std::move(metadata)}, write_income_files_{write_income_files},
      income_layout_{std::move(income_layout)} {
    csv_path_ = base_path;
    csv_path_.replace_extension("csv");

    json_path_ = base_path;
    json_path_.replace_extension("json");

    std::filesystem::create_directories(csv_path_.parent_path());

    // Truncate rather than append. The baseline opens with std::ofstream::app, so a second run
    // writing to the same path silently concatenates two runs' rows into one file.
    csv_.open(csv_path_, std::ofstream::out | std::ofstream::trunc);
    if (!csv_.is_open()) {
        throw std::invalid_argument(
            fmt::format("Cannot open output file: {}", csv_path_.string()));
    }

    json_.open(json_path_, std::ofstream::out | std::ofstream::trunc);
    if (!json_.is_open()) {
        throw std::invalid_argument(
            fmt::format("Cannot open output file: {}", json_path_.string()));
    }

    if (write_income_files_) {
        for (const auto income : income_layout_.strata) {
            auto path = csv_path_;
            path.replace_filename(fmt::format("{}_{}.csv", csv_path_.stem().string(),
                                              core::income_file_name(income)));

            // Every configured stratum gets a file, whether or not anybody falls into it this
            // year. The baseline opens a stratum's file on first use, so a year with nobody in a
            // stratum leaves a gap in that file's calendar.
            auto &stream = income_csv_[income];
            stream.open(path, std::ofstream::out | std::ofstream::trunc);
            if (!stream.is_open()) {
                throw std::invalid_argument(
                    fmt::format("Cannot open output file: {}", path.string()));
            }
            income_paths_[income] = path;
        }
    }

    write_json_begin();
}

ResultWriter::~ResultWriter() { close(); }

std::vector<std::filesystem::path> ResultWriter::paths() const {
    std::vector<std::filesystem::path> result{csv_path_, json_path_};
    for (const auto &[income, path] : income_paths_) {
        result.push_back(path);
    }
    return result;
}

void ResultWriter::write(const sim::ResultRow &row) {
    if (closed_) {
        throw diag::InternalError("the result writer has already been closed");
    }

    if (!csv_header_written_) {
        write_csv_header(row.result.series);
        csv_header_written_ = true;
    }

    write_csv_rows(row);
    write_income_rows(row);
    write_json_entry(row);
}

void ResultWriter::write_csv_header(const model::DataSeries &series) {
    csv_ << "source,run,time,gender_name,index_id";
    for (const auto &channel : series.channels()) {
        csv_ << ',' << channel;
    }
    csv_ << '\n';

    for (auto &[income, stream] : income_csv_) {
        stream << "source,run,time,gender_name,index_id";
        for (const auto &channel : series.channels()) {
            stream << ',' << channel;
        }
        stream << '\n';
    }
}

void ResultWriter::write_csv_rows(const sim::ResultRow &row) {
    const auto &series = row.result.series;

    // Male then female for each index, which is the baseline's row order within a year.
    for (std::size_t index = 0; index < series.size(); ++index) {
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            csv_ << row.source_name << ',' << row.run << ',' << row.time << ','
                 << (gender == core::Gender::male ? "male" : "female") << ',' << index;

            for (const auto &channel : series.channels()) {
                csv_ << ',' << format_value(series.at(gender, channel).at(index));
            }

            csv_ << '\n';
        }
    }
}

void ResultWriter::write_income_rows(const sim::ResultRow &row) {
    if (income_csv_.empty()) {
        return;
    }

    const auto &series = row.result.series;
    if (!series.has_income_channels()) {
        return;
    }

    for (auto &[income, stream] : income_csv_) {
        for (std::size_t index = 0; index < series.size(); ++index) {
            for (const auto gender : {core::Gender::male, core::Gender::female}) {
                stream << row.source_name << ',' << row.run << ',' << row.time << ','
                       << (gender == core::Gender::male ? "male" : "female") << ',' << index;

                for (const auto &channel : series.channels()) {
                    // A channel with no income-stratified counterpart reads as zero rather than
                    // being absent, so every stratum file has the same columns as the main one.
                    double value = 0.0;
                    try {
                        value = series.at(gender, income, channel).at(index);
                    } catch (const std::out_of_range &) {
                        value = 0.0;
                    }
                    stream << ',' << format_value(value);
                }

                stream << '\n';
            }
        }
    }
}

void ResultWriter::write_json_begin() {
    nlohmann::json header;
    header["model"] = metadata_.model;
    header["version"] = metadata_.version;
    header["intervention"] = metadata_.intervention.empty() ? "none" : metadata_.intervention;
    header["job_id"] = metadata_.job_id;

    // The seed actually used, and every run's derived seed. This is the provenance record the
    // baseline gets wrong: it writes `seed().value_or(0)`, so an unseeded run claims seed 0 and
    // re-running with 0 does not reproduce it (audit B-06, N-3).
    header["seed"] = metadata_.seed;
    header["run_seeds"] = metadata_.run_seeds;

    header["config_file"] = metadata_.config_path.string();
    header["config_sha256"] = metadata_.config_sha256;
    header["country"] = metadata_.country;
    header["start_time"] = metadata_.start_time;
    header["stop_time"] = metadata_.stop_time;
    header["trial_runs"] = metadata_.trial_runs;
    header["cohort_size"] = metadata_.cohort_size;
    header["started_utc"] = utc_timestamp();
    header["result_file"] = csv_path_.filename().string();

    json_ << "{\n  \"metadata\": " << header.dump(2) << ",\n  \"results\": [";
}

void ResultWriter::write_json_entry(const sim::ResultRow &row) {
    nlohmann::json entry;
    entry["source"] = row.source_name;
    entry["run"] = row.run;
    entry["time"] = row.time;
    entry["population_size"] = row.result.population_size;
    entry["number_alive"] = nlohmann::json{{"male", row.result.number_alive.male},
                                           {"female", row.result.number_alive.female}};
    entry["number_dead"] = row.result.number_dead;
    entry["number_emigrated"] = row.result.number_emigrated;
    entry["average_age"] = to_json(row.result.average_age);
    entry["indicators"] =
        nlohmann::json{{"years_of_life_lost", row.result.indicators.years_of_life_lost},
                       {"years_lived_with_disability",
                        row.result.indicators.years_lived_with_disability},
                       {"disability_adjusted_life_years",
                        row.result.indicators.disability_adjusted_life_years}};

    // Ordered maps throughout, so the JSON's key order is stated rather than incidental.
    for (const auto &[name, value] : row.result.risk_factor_average) {
        entry["risk_factor_average"][name] = to_json(value);
    }
    for (const auto &[name, value] : row.result.disease_prevalence) {
        entry["disease_prevalence"][name] = to_json(value);
    }
    for (const auto &[number, value] : row.result.comorbidity) {
        entry["comorbidity"][std::to_string(number)] = to_json(value);
    }
    for (const auto &[name, value] : row.result.metrics) {
        entry["metrics"][name] = value;
    }

    json_ << (json_first_entry_ ? "\n    " : ",\n    ") << entry.dump();
    json_first_entry_ = false;
}

void ResultWriter::write_json_end() {
    json_ << "\n  ],\n  \"finished_utc\": \"" << utc_timestamp() << "\"\n}\n";
}

void ResultWriter::close() {
    if (closed_) {
        return;
    }
    closed_ = true;

    if (json_.is_open()) {
        write_json_end();
        json_.flush();
        json_.close();
    }

    if (csv_.is_open()) {
        csv_.flush();
        csv_.close();
    }

    for (auto &[income, stream] : income_csv_) {
        if (stream.is_open()) {
            stream.flush();
            stream.close();
        }
    }
}

} // namespace hgps::output
