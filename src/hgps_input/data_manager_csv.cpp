#include "data_manager.h"

#include <map>
#include <stdexcept>

namespace hgps::input {

std::map<std::string, std::size_t>
DataManager::create_fields_index_mapping(const std::vector<std::string> &column_names,
                                         const std::vector<std::string> &fields) {
    auto mapping = std::map<std::string, std::size_t>();
    for (const auto &field : fields) {
        auto field_index = core::case_insensitive::index_of(column_names, field);
        if (field_index < 0) {
            throw std::out_of_range(
                fmt::format("File-based store, required field {} not found", field));
        }

        mapping.emplace(field, field_index);
    }

    return mapping;
}

} // namespace hgps::input