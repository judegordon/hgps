#pragma once

#include "HealthGPS/individual_tracking_message.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace hgps {

class IndividualIDTrackingWriter {
  public:
    IndividualIDTrackingWriter() = delete;

    explicit IndividualIDTrackingWriter(const std::filesystem::path &base_result_path);

    IndividualIDTrackingWriter(const IndividualIDTrackingWriter &) = delete;
    IndividualIDTrackingWriter &operator=(const IndividualIDTrackingWriter &) = delete;
    IndividualIDTrackingWriter(IndividualIDTrackingWriter &&) = default;
    IndividualIDTrackingWriter &operator=(IndividualIDTrackingWriter &&) = default;

    void write(const IndividualTrackingEventMessage &message);

  private:
    static std::filesystem::path make_tracking_path(const std::filesystem::path &base_result_path);
    void write_header(const IndividualTrackingEventMessage &message);
    void write_row(unsigned int run, int time, const std::string &scenario,
                   const IndividualTrackingRow &row,
                   const std::vector<std::string> &risk_factor_columns);

    std::ofstream stream_;
    std::mutex mutex_;
    bool header_written_{false};
    std::vector<std::string> risk_factor_columns_;
    unsigned int write_count_{0}; // MAHIMA: for periodic flush (every 5 writes)
};

} // namespace hgps
