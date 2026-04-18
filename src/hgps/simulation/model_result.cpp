#include "model_result.h"

namespace hgps {

ModelResult::ModelResult(unsigned int sample_size) : series{sample_size} {}

int ModelResult::number_of_recyclable() const noexcept {
    return population_size - (number_alive.total() + number_dead + number_emigrated);
}

} // namespace hgps