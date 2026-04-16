#pragma once
#include "hgps_core/forward_type.h"

#include <map>
#include <string>
#include <vector>

namespace hgps {

class DataSeries {
  public:
    DataSeries() = delete;

    DataSeries(std::size_t sample_size);

    std::vector<double> &operator()(core::Gender gender, const std::string &key);

    std::vector<double> &at(core::Gender gender, const std::string &key);

    const std::vector<double> &at(core::Gender gender, const std::string &key) const;

    std::vector<double> &at(core::Gender gender, core::Income income, const std::string &key);

    const std::vector<double> &at(core::Gender gender, core::Income income,
                                  const std::string &key) const;

    const std::vector<std::string> &channels() const noexcept;

    void add_channel(std::string key);

    void add_channels(const std::vector<std::string> &keys);

    void add_income_channels(const std::vector<std::string> &keys);

    void add_income_channels_for_categories(const std::vector<std::string> &keys,
                                            const std::vector<core::Income> &income_categories);

    std::size_t size() const noexcept;

    std::size_t sample_size() const noexcept;

    std::vector<core::Income> get_available_income_categories() const;

    bool has_income_category(core::Gender gender, core::Income income) const;

    std::string income_category_to_string(core::Income income) const;

  private:
    std::size_t sample_size_;
    std::vector<std::string> channels_;
    std::map<core::Gender, std::map<std::string, std::vector<double>>> data_;
    std::map<core::Gender, std::map<core::Income, std::map<std::string, std::vector<double>>>>
        income_data_;
};
} // namespace hgps
