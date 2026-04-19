#include "weight_model.h"

#include <stdexcept>

namespace hgps {

std::string weight_category_to_string(const WeightCategory value) {
    switch (value) {
    case WeightCategory::normal:
        return "normal";
    case WeightCategory::overweight:
        return "overweight";
    case WeightCategory::obese:
        return "obese";
    default:
        throw std::invalid_argument("Unknown weight category item");
    }
}

} // namespace hgps