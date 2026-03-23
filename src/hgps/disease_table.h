#pragma once
#include <map>
#include <string>

#include "hgps_core/forward_type.h"
#include "hgps_core/poco.h"

namespace hgps {

struct MeasureKey final {
    static inline const std::string prevalence{"prevalence"};
    static inline const std::string mortality{"mortality"};
    static inline const std::string remission{"remission"};
    static inline const std::string incidence{"incidence"};
};

class DiseaseMeasure {
  public:
    DiseaseMeasure() = default;

    DiseaseMeasure(std::map<int, double> measures);

    std::size_t size() const noexcept;

    double at(int measure_id) const;

    double operator[](int measure_id) const;

  private:
    std::map<int, double> measures_;
};

class DiseaseTable {
  public:
    DiseaseTable() = delete;

    DiseaseTable(const core::DiseaseInfo &info, std::map<std::string, int> &&measures,
                 std::map<int, std::map<core::Gender, DiseaseMeasure>> &&data);

    const core::DiseaseInfo &info() const noexcept;

    std::size_t size() const noexcept;

    std::size_t rows() const noexcept;

    std::size_t cols() const noexcept;

    bool contains(const int age) const noexcept;

    const std::map<std::string, int> &measures() const noexcept;

    int at(const std::string &measure) const;

    int operator[](const std::string &measure) const;

    DiseaseMeasure &operator()(const int age, const core::Gender gender);

    const DiseaseMeasure &operator()(const int age, const core::Gender gender) const;

  private:
    core::DiseaseInfo info_;
    std::map<std::string, int> measures_;
    std::map<int, std::map<core::Gender, DiseaseMeasure>> data_;
};
} // namespace hgps
