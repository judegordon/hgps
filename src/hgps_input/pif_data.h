#pragma once

#include "HealthGPS.Core/disease.h"
#include "HealthGPS.Core/identifier.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace hgps::input {

struct PIFDataItem {
    int age;
    core::Gender gender;
    int year_post_intervention;
    double pif_value;

    PIFDataItem()
        : age(0), gender(core::Gender::female), year_post_intervention(0), pif_value(0.0) {}

    PIFDataItem(int age, core::Gender gender, int year_post_intervention, double pif_value)
        : age(age), gender(gender), year_post_intervention(year_post_intervention),
          pif_value(pif_value) {}
};

class PIFTable {
  public:
    PIFTable() = default;

    double get_pif_value(int age, core::Gender gender, int year_post_intervention) const;

    void add_item(const PIFDataItem &item);

    void build_hash_table();

    bool has_data() const noexcept { return !data_.empty(); }

    std::size_t size() const noexcept { return data_.size(); }

    void clear() noexcept {
        data_.clear();
        direct_array_.clear();
    }

  private:
    std::vector<PIFDataItem> data_;

    // Direct array for TRUE O(1) access - no lookups!
    std::vector<PIFDataItem> direct_array_;

    int min_age_{0}, max_age_{0}, min_year_{0}, max_year_{0};
    int age_range_{1}, year_range_{1};
};

class PIFData {
  public:
    PIFData() = default;

    void add_scenario_data(const std::string &scenario, PIFTable &&table);

    const PIFTable *get_scenario_data(const std::string &scenario) const;

    bool has_data() const noexcept { return !scenario_data_.empty(); }

    std::size_t scenario_count() const noexcept { return scenario_data_.size(); }

    std::vector<std::string> get_scenario_names() const;

    void clear() noexcept { scenario_data_.clear(); }

  private:
    std::unordered_map<std::string, PIFTable> scenario_data_;
};

} // namespace hgps::input
