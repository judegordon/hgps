#pragma once
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
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
    ResultFileWriter(ResultFileWriter &&other) noexcept;
    ResultFileWriter &operator=(ResultFileWriter &&other) noexcept;

    ~ResultFileWriter();
    void write(const hgps::ResultEventMessage &message) override;

  private:
    std::ofstream stream_;
    std::ofstream csvstream_;
    std::map<core::Income, std::ofstream> income_csvstreams_;
    std::map<core::Income, bool> income_first_row_;
    // MAHIMA: One mutex per income stream so income CSV files can be written in parallel.
    // unique_ptr so the map is movable (std::mutex is not movable).
    std::map<core::Income, std::unique_ptr<std::mutex>> income_mutexes_;
    ExperimentInfo info_;
    bool write_income_csv_{true};
    std::filesystem::path base_filename_;
    std::atomic<bool> first_row_{true};
    std::mutex lock_mutex_;

    void write_json_begin(const std::filesystem::path &output);
    void write_json_end();
    std::string to_json_string(const hgps::ResultEventMessage &message);
    void write_csv_header(const hgps::ResultEventMessage &message);
    void write_csv_channels(const hgps::ResultEventMessage &message);

    void write_income_based_csv_files(const hgps::ResultEventMessage &message);

    void write_income_csv_header(const hgps::ResultEventMessage &message,
                                 std::ofstream &income_csv);

    void write_income_aggregated_csv_data(const hgps::ResultEventMessage &message,
                                          core::Income income, std::ofstream &income_csv);

    static void write_income_csv_data(const hgps::ResultEventMessage &message, core::Income income,
                                      std::ofstream &income_csv);

    std::string generate_income_filename(const std::string &base_filename,
                                         core::Income income) const;

    std::string income_category_to_string(core::Income income) const;

    std::vector<core::Income>
    get_available_income_categories(const hgps::ResultEventMessage &message) const;

    void initialize_income_streams(const hgps::ResultEventMessage &message);
    void write_income_csv_channels(const hgps::ResultEventMessage &message);
};
} // namespace hgps
