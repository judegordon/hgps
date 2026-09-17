#include "internal_error.h"

#include <filesystem>

#include <fmt/format.h>

namespace hgps::diag {

InternalError::InternalError(const std::string &message, std::source_location location)
    : std::runtime_error(message), location_{location} {}

std::string InternalError::describe() const {
    // The file name is shortened to its last two components: an absolute build path tells the
    // reader nothing, while "model/demographic.cpp" tells them where to look.
    const std::filesystem::path path{location_.file_name()};
    std::string where = path.filename().string();
    if (path.has_parent_path() && !path.parent_path().filename().empty()) {
        where = (path.parent_path().filename() / path.filename()).string();
    }

    return fmt::format("{} [{}:{} {}]", what(), where, location_.line(), location_.function_name());
}

} // namespace hgps::diag
