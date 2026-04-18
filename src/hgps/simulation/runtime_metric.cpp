#include "runtime_metric.h"

namespace hgps {

double &RuntimeMetric::operator[](const std::string &metric_key) {
    return metrics_[metric_key];
}

void RuntimeMetric::clear() noexcept {
    metrics_.clear();
}

void RuntimeMetric::reset() noexcept {
    for (auto &[_, value] : metrics_) {
        value = 0.0;
    }
}

} // namespace hgps