#pragma once
#include <string>
#include <unordered_map>

namespace hgps {

class RuntimeMetric {
  public:
    using IteratorType = std::unordered_map<std::string, double>::iterator;

    using ConstIteratorType = std::unordered_map<std::string, double>::const_iterator;

    std::size_t size() const noexcept;

    double &operator[](const std::string &metric_key);

    double &at(const std::string &metric_key);

    double at(const std::string &metric_key) const;

    bool contains(const std::string &metric_key) const noexcept;

    bool emplace(const std::string &metric_key, double value);

    bool erase(const std::string &metric_key);

    void clear() noexcept;

    void reset() noexcept;

    bool empty() const noexcept;

    IteratorType begin() noexcept { return metrics_.begin(); }

    IteratorType end() noexcept { return metrics_.end(); }

    ConstIteratorType begin() const noexcept { return metrics_.cbegin(); }

    ConstIteratorType end() const noexcept { return metrics_.cend(); }

    ConstIteratorType cbegin() const noexcept { return metrics_.cbegin(); }

    ConstIteratorType cend() const noexcept { return metrics_.cend(); }

  private:
    std::unordered_map<std::string, double> metrics_;
};
} // namespace hgps
