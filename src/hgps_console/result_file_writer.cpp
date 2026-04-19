// result_file_writer.cpp
#include "result_file_writer.h"

#include "hgps_core/forward_type.h"
#include "hgps/data/data_series.h"

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace hgps {
namespace {

double income_category_numeric(core::Income inc) {
    switch (inc) {
    case core::Income::low:
        return 1.0;
    case core::Income::lowermiddle:
    case core::Income::middle:
        return 2.0;
    case core::Income::uppermiddle:
        return 3.0;
    case core::Income::high:
        return 4.0;
    default:
        return 0.0;
    }
}

} // namespace

ResultFileWriter::ResultFileWriter(const std::filesystem::path &file_name, ExperimentInfo info,
                                   bool write_income_csv)
    : info_{std::move(info)}, write_income_csv_{write_income_csv}, base_filename_{file_name} {
    stream_.open(file_name, std::ofstream::out | std::ofstream::app);
    if (!stream_.is_open()) {
        throw std::invalid_argument(fmt::format("Cannot open output file: {}", file_name.string()));
    }

    auto csv_filename = file_name;
    csv_filename.replace_extension("csv");
    csvstream_.open(csv_filename, std::ofstream::out | std::ofstream::app);
    if (!csvstream_.is_open()) {
        throw std::invalid_argument(
            fmt::format("Cannot open output file: {}", csv_filename.string()));
    }

    write_json_begin(file_name);
}

ResultFileWriter::~ResultFileWriter() {
    if (stream_.is_open()) {
        write_json_end();
        stream_.flush();
        stream_.close();
    }

    if (csvstream_.is_open()) {
        csvstream_.flush();
        csvstream_.close();
    }

    for (auto &[income, stream] : income_csvstreams_) {
        (void)income;
        if (stream.is_open()) {
            stream.flush();
            stream.close();
        }
    }
}

void ResultFileWriter::write(const hgps::ResultEventMessage &message) {
    std::scoped_lock lock(lock_mutex_);

    if (first_row_) {
        first_row_ = false;
        write_csv_header(message);
    } else {
        stream_ << ",";
    }

    stream_ << to_json_string(message);
    write_csv_channels(message);

    if (!write_income_csv_ || !message.content.population_by_income.has_value()) {
        return;
    }

    if (income_csvstreams_.empty()) {
        initialize_income_streams(message);
    }

    for (const auto income : get_available_income_categories(message)) {
        auto &income_stream = income_csvstreams_.at(income);
        auto &is_first_row = income_first_row_.at(income);

        if (is_first_row) {
            is_first_row = false;
            write_income_csv_header(message, income_stream);
        }

        write_income_csv_data(message, income, income_stream);
    }
}

void ResultFileWriter::write_json_begin(const std::filesystem::path &output) {
    using json = nlohmann::ordered_json;

    json msg = {{"experiment",
                 {{"model", info_.model},
                  {"version", info_.version},
                  {"intervention", info_.intervention},
                  {"job_id", info_.job_id},
                  {"custom_seed", info_.seed},
                  {"output_filename", output.filename().string()}}},
                {"result", {1, 2}}};

    const auto json_header = msg.dump();
    const auto array_start = json_header.find_last_of('[');
    stream_ << json_header.substr(0, array_start + 1);
    stream_.flush();
}

void ResultFileWriter::write_json_end() { stream_ << "]}"; }

std::string ResultFileWriter::to_json_string(const hgps::ResultEventMessage &message) const {
    using json = nlohmann::ordered_json;

    json msg = {
        {"id", message.id()},
        {"source", message.source},
        {"run", message.run_number},
        {"time", message.model_time},
        {"population",
         {{"size", message.content.population_size},
          {"alive", message.content.number_alive.total()},
          {"alive_male", message.content.number_alive.males},
          {"alive_female", message.content.number_alive.females},
          {"migrating", message.content.number_emigrated},
          {"dead", message.content.number_dead},
          {"recycle", message.content.number_of_recyclable()}}},
        {"average_age",
         {{"male", message.content.average_age.male},
          {"female", message.content.average_age.female}}},
        {"indicators",
         {{"YLL", message.content.indicators.years_of_life_lost},
          {"YLD", message.content.indicators.years_lived_with_disability},
          {"DALY", message.content.indicators.disability_adjusted_life_years}}},
    };

    for (const auto &factor : message.content.risk_factor_average) {
        msg["risk_factors_average"][factor.first] = {{"male", factor.second.male},
                                                     {"female", factor.second.female}};
    }

    for (const auto &disease : message.content.disease_prevalence) {
        msg["disease_prevalence"][disease.first] = {{"male", disease.second.male},
                                                    {"female", disease.second.female}};
    }

    for (const auto &item : message.content.comorbidity) {
        msg["comorbidities"][std::to_string(item.first)] = {{"male", item.second.male},
                                                            {"female", item.second.female}};
    }

    msg["metrics"] = message.content.metrics;
    return msg.dump();
}

void ResultFileWriter::write_csv_header(const hgps::ResultEventMessage &message) {
    csvstream_ << "source,run,time,gender_name,index_id";
    for (const auto &chan : message.content.series.channels()) {
        csvstream_ << "," << chan;
    }
    csvstream_ << '\n';
}

void ResultFileWriter::write_csv_channels(const hgps::ResultEventMessage &message) {
    using namespace hgps::core;

    const auto &series = message.content.series;

    for (auto index = 0u; index < series.sample_size(); ++index) {
        std::stringstream mss;
        std::stringstream fss;

        mss << message.source << ',' << message.run_number << ',' << message.model_time << ','
            << "male"
            << ',' << index;
        fss << message.source << ',' << message.run_number << ',' << message.model_time << ','
            << "female"
            << ',' << index;

        for (const auto &key : series.channels()) {
            mss << ',' << series.at(Gender::male, key).at(index);
            fss << ',' << series.at(Gender::female, key).at(index);
        }

        csvstream_ << mss.str() << '\n' << fss.str() << '\n';
    }
}

void ResultFileWriter::initialize_income_streams(const hgps::ResultEventMessage &message) {
    for (const auto income : get_available_income_categories(message)) {
        const auto income_filename = generate_income_filename(base_filename_.string(), income);
        std::ofstream income_stream(income_filename, std::ofstream::out | std::ofstream::app);

        if (!income_stream.is_open()) {
            throw std::invalid_argument(
                fmt::format("Cannot open income-based output file: {}", income_filename));
        }

        income_csvstreams_.emplace(income, std::move(income_stream));
        income_first_row_.emplace(income, true);
    }
}

void ResultFileWriter::write_income_csv_header(const hgps::ResultEventMessage &message,
                                               std::ofstream &income_csv) {
    income_csv << "source,run,time,gender_name,index_id";
    for (const auto &chan : message.content.series.channels()) {
        income_csv << "," << chan;
    }
    income_csv << '\n';
}

void ResultFileWriter::write_income_csv_data(const hgps::ResultEventMessage &message,
                                             core::Income income, std::ofstream &income_csv) {
    using namespace hgps::core;

    const auto &series = message.content.series;
    const double file_income_category_value = income_category_numeric(income);

    for (auto index = 0u; index < series.sample_size(); ++index) {
        double count_m = 0.0;
        double count_f = 0.0;

        try {
            count_m = series.at(Gender::male, income, "count").at(index);
            count_f = series.at(Gender::female, income, "count").at(index);
        } catch (const std::out_of_range &) {
            continue;
        }

        auto write_row = [&](Gender gender, double count) {
            if (count <= 0.0) {
                return;
            }

            std::stringstream ss;
            ss << message.source << ',' << message.run_number << ',' << message.model_time << ','
               << (gender == Gender::male ? "male" : "female") << ',' << index;

            for (const auto &key : series.channels()) {
                if (key == "mean_income_category") {
                    ss << ',' << file_income_category_value;
                    continue;
                }
                if (key == "std_income_category") {
                    ss << ',' << 0.0;
                    continue;
                }

                double value = 0.0;
                try {
                    value = series.at(gender, income, key).at(index);
                } catch (const std::out_of_range &) {
                }

                ss << ',' << value;
            }

            income_csv << ss.str() << '\n';
        };

        write_row(Gender::male, count_m);
        write_row(Gender::female, count_f);
    }
}

std::string ResultFileWriter::generate_income_filename(const std::string &base_filename,
                                                       core::Income income) const {
    const auto income_suffix = income_category_to_string(income);
    const auto dot_pos = base_filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        return base_filename.substr(0, dot_pos) + "_" + income_suffix + ".csv";
    }
    return base_filename + "_" + income_suffix + ".csv";
}

std::string ResultFileWriter::income_category_to_string(core::Income income) const {
    switch (income) {
    case core::Income::low:
        return "LowIncome";
    case core::Income::lowermiddle:
        return "LowerMiddleIncome";
    case core::Income::middle:
        return "MiddleIncome";
    case core::Income::uppermiddle:
        return "UpperMiddleIncome";
    case core::Income::high:
        return "HighIncome";
    case core::Income::unknown:
    default:
        return "UnknownIncome";
    }
}

std::vector<core::Income>
ResultFileWriter::get_available_income_categories(const hgps::ResultEventMessage &message) const {
    std::vector<core::Income> categories;
    std::unordered_set<core::Income> seen;

    if (!message.content.population_by_income.has_value()) {
        return categories;
    }

    const auto &pop_by_income = message.content.population_by_income.value();

    if (pop_by_income.low > 0 && !seen.contains(core::Income::low)) {
        categories.push_back(core::Income::low);
        seen.insert(core::Income::low);
    }
    if (pop_by_income.lowermiddle > 0 && !seen.contains(core::Income::lowermiddle)) {
        categories.push_back(core::Income::lowermiddle);
        seen.insert(core::Income::lowermiddle);
    }
    if (pop_by_income.middle > 0 && !seen.contains(core::Income::middle)) {
        categories.push_back(core::Income::middle);
        seen.insert(core::Income::middle);
    }
    if (pop_by_income.uppermiddle > 0 && !seen.contains(core::Income::uppermiddle)) {
        categories.push_back(core::Income::uppermiddle);
        seen.insert(core::Income::uppermiddle);
    }
    if (pop_by_income.high > 0 && !seen.contains(core::Income::high)) {
        categories.push_back(core::Income::high);
        seen.insert(core::Income::high);
    }

    return categories;
}

} // namespace hgps