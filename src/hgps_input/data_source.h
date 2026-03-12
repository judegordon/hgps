#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace hgps::input {

class DataSource {
  public:
    explicit DataSource(std::string source);

    explicit DataSource(std::string source, const std::filesystem::path &root_path);

    virtual ~DataSource() = default;

    // Copy and move constructors
    DataSource(const DataSource &) noexcept = default;
    DataSource(DataSource &&) noexcept = default;
    DataSource &operator=(const DataSource &) noexcept = default;
    DataSource &operator=(DataSource &&) noexcept = default;

    virtual std::string compute_file_hash(const std::filesystem::path &zip_file_path) const;

    std::filesystem::path get_data_directory() const;

  protected:
    std::string source_;
};
} // namespace hgps::input
