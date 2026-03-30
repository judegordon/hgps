#include "data_manager.h"
#include "schema.h"

#include <fmt/color.h>

#include <utility>

namespace {
constexpr const char *IndexFileName = "index.json";
constexpr const char *DataIndexSchemaFileName = "data_index/data_index.json";
constexpr int DataIndexSchemaVersion = 1;

nlohmann::json read_input_files_from_directory(const std::filesystem::path &data_path) {
    return hgps::input::load_and_validate_json(data_path / IndexFileName, DataIndexSchemaFileName,
                                               DataIndexSchemaVersion);
}
} // anonymous namespace

namespace hgps::input {

DataManager::DataManager(std::filesystem::path data_path, VerboseMode verbosity)
    : root_(std::move(data_path)), verbosity_(verbosity),
      index_(read_input_files_from_directory(root_)) {}

void DataManager::notify_warning(std::string_view message) const {
    if (verbosity_ == VerboseMode::none) {
        return;
    }

    fmt::print(fg(fmt::color::dark_salmon), "File-based store, {}\n", message);
}

} // namespace hgps::input