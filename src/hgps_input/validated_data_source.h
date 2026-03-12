#pragma once

#include "data_source.h"

#include <filesystem>
#include <string>

namespace hgps::input {
class ValidatedDataSource : public DataSource {
  public:
    explicit ValidatedDataSource(std::string source, const std::filesystem::path &root_path,
                                 std::string file_hash);

    ~ValidatedDataSource() override = default;

    std::string compute_file_hash(const std::filesystem::path &zip_file_path) const override;

  private:
    std::string file_hash_;
};
} // namespace hgps::input
