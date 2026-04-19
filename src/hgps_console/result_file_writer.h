// result_file_writer.h
#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include "model_info.h"
#include "result_writer.h"

namespace hgps {

class ResultFileWriter final : public ResultWriter {
  public:
    ResultFileWriter() = delete;
    ResultFileWriter(const std::filesystem::path &file_name, ExperimentInfo info,
                     bool write_income_csv = true);

    ResultFileWriter(const ResultFileWriter &) = delete;
    ResultFileWriter &operator=(const ResultFileWriter &) = delete;
    ResultFileWriter(ResultFileWriter &&) = delete;
    ResultFileWriter &operator=(ResultFileWriter &&) = delete;

    ~ResultFileWriter() override;

    void write(const hgps::ResultEventMessage &message) override;

  private:
    std::ofstream stream_;
    std::ofstream csvstream_;
    std::map<core::Income, std::ofstream> income_csvstreams_;
    std::map<core::Income, bool> income_first_row_;
    ExperimentInfo info_;
    bool write_income_csv_{true};
    std::filesystem::path base_filename_;
    bool first_row_{true};
    std::mutex lock_mutex_;

    void write_json_begin(const std::filesystem::path &output);
    void write_json_end();
    [[nodiscard]] std::string to_json_string(const hgps::ResultEventMessage &message) const;

    void write_csv_header(const hgps::ResultEventMessage &message);
    void write_csv_channels(const hgps::ResultEventMessage &message);

    void initialize_income_streams(const hgps::ResultEventMessage &message);
    void write_income_csv_header(const hgps::ResultEventMessage &message,
                                 std::ofstream &income_csv);
    static void write_income_csv_data(const hgps::ResultEventMessage &message, core::Income income,
                                      std::ofstream &income_csv);

    [[nodiscard]] std::string generate_income_filename(const std::string &base_filename,
                                                       core::Income income) const;
    [[nodiscard]] std::string income_category_to_string(core::Income income) const;
    [[nodiscard]] std::vector<core::Income>
    get_available_income_categories(const hgps::ResultEventMessage &message) const;
};

} // namespace hgps