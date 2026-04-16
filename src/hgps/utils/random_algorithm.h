#pragma once

#include "mtrandom.h"
#include <functional>

namespace hgps {
class Random {
  public:
    void seed(unsigned int seed) noexcept;

    int next_int();

    int next_int(int max_value);

    int next_int(int min_value, int max_value);

    double next_double() noexcept;

    double next_normal();

    double next_normal(double mean, double standard_deviation);

    int next_empirical_discrete(const std::vector<int> &values, const std::vector<float> &cdf);

    int next_empirical_discrete(const std::vector<int> &values, const std::vector<double> &cdf);

  private:
    MTRandom32 engine_;

    int next_int_internal(int min_value, int max_value);
    double next_uniform_internal(double min_value, double max_value);
    double next_normal_internal(double mean, double standard_deviation);
};
} // namespace hgps
