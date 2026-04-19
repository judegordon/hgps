#pragma once

#include "hgps_core/forward_type.h"
#include "hgps_core/types/identifier.h"

#include <functional>
#include <map>
#include <string>

namespace hgps {

enum struct DiseaseStatus : uint8_t {
    free,
    active
};

struct Disease {
    DiseaseStatus status{};
    int start_time{};
    int time_since_onset{-1};

    Disease clone() const noexcept {
        return Disease{
            .status = status,
            .start_time = start_time,
            .time_since_onset = time_since_onset,
        };
    }
};

struct Person {
    Person() = delete;

    Person(const core::Gender gender) = delete;

    explicit Person(std::size_t id) noexcept;

    Person(const core::Gender gender, std::size_t id) noexcept;

    void set_id(std::size_t id) noexcept;

    std::size_t id() const noexcept;

    core::Gender gender{core::Gender::unknown};
    unsigned int age{};
    core::Sector sector{core::Sector::unknown};
    std::string region{"unknown"};
    std::string ethnicity{"unknown"};
    core::Income income{core::Income::unknown};
    double income_continuous{0.0};
    double physical_activity{0.0};
    double ses{};
    std::map<core::Identifier, double> risk_factors;
    std::map<core::Identifier, Disease> diseases;

    bool is_alive() const noexcept;

    bool has_emigrated() const noexcept;

    unsigned int time_of_death() const noexcept;

    unsigned int time_of_migration() const noexcept;

    bool is_active() const noexcept;

    double get_risk_factor_value(const core::Identifier &key) const;

    float gender_to_value() const;

    static float gender_to_value(core::Gender gender);

    std::string gender_to_string() const;

    bool over_18() const noexcept;

    float sector_to_value() const;

    float income_to_value() const;

    float region_to_value() const;

    float ethnicity_to_value() const;

    void emigrate(unsigned int time);

    void die(unsigned int time);

  private:
    std::size_t id_{};
    bool is_alive_{true};
    bool has_emigrated_{false};
    unsigned int time_of_death_{};
    unsigned int time_of_migration_{};

    static std::map<core::Identifier, std::function<double(const Person &)>> current_dispatcher;
};

} // namespace hgps