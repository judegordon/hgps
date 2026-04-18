#pragma once

#include <string>
#include <unordered_map>

namespace hgps {

class RuntimeMetric {
  public:
    using IteratorType = std::unordered_map<std::string, double>::iterator;
    using ConstIteratorType = std::unordered_map<std::string, double>::const_iterator;

    double &operator[](const std::string &metric_key);

    void clear() noexcept;
    void reset() noexcept;

    IteratorType begin() noexcept { return metrics_.begin(); }
    IteratorType end() noexcept { return metrics_.end(); }

    ConstIteratorType begin() const noexcept { return metrics_.cbegin(); }
    ConstIteratorType end() const noexcept { return metrics_.cend(); }

  private:
    std::unordered_map<std::string, double> metrics_;
};

} // namespace hgps