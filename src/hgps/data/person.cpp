#include "person.h"

#include "hgps_core/diagnostics/internal_error.h"

#include <cmath>
#include <stdexcept>

using hgps::core::operator""_id;

namespace hgps {

std::map<core::Identifier, std::function<double(const Person &)>> Person::current_dispatcher{
    {"Intercept"_id, [](const Person &) { return 1.0; }},
    {"Gender"_id, [](const Person &p) { return p.gender_to_value(); }},
    {"Age"_id, [](const Person &p) { return static_cast<double>(p.age); }},
    {"Age2"_id, [](const Person &p) { return std::pow(p.age, 2); }},
    {"Age3"_id, [](const Person &p) { return std::pow(p.age, 3); }},
    {"Over18"_id, [](const Person &p) { return static_cast<double>(p.over_18()); }},
    {"Sector"_id, [](const Person &p) { return p.sector_to_value(); }},
    {"Region"_id, [](const Person &p) { return p.region_to_value(); }},
    {"Ethnicity"_id, [](const Person &p) { return p.ethnicity_to_value(); }},
    {"Income"_id, [](const Person &p) { return p.income_to_value(); }},
    {"SES"_id, [](const Person &p) { return p.ses; }},

    {"gender2"_id, [](const Person &p) { return p.gender_to_value(); }},
    {"age1"_id, [](const Person &p) { return static_cast<double>(p.age); }},
    {"age2"_id, [](const Person &p) { return std::pow(p.age, 2); }},
    {"ethnicity2"_id, [](const Person &p) { return p.ethnicity_to_value() == 2.0 ? 1.0 : 0.0; }},
    {"ethnicity3"_id, [](const Person &p) { return p.ethnicity_to_value() == 3.0 ? 1.0 : 0.0; }},
    {"ethnicity4"_id, [](const Person &p) { return p.ethnicity_to_value() == 4.0 ? 1.0 : 0.0; }},
    {"income"_id, [](const Person &p) { return p.income_to_value(); }},
    {"region2"_id, [](const Person &p) { return p.region_to_value() == 2.0 ? 1.0 : 0.0; }},
    {"region3"_id, [](const Person &p) { return p.region_to_value() == 3.0 ? 1.0 : 0.0; }},
    {"region4"_id, [](const Person &p) { return p.region_to_value() == 4.0 ? 1.0 : 0.0; }},
};

Person::Person(std::size_t id) noexcept : id_{id} {}

Person::Person(const core::Gender birth_gender, std::size_t id) noexcept
    : gender{birth_gender}, id_{id} {}

void Person::set_id(std::size_t id) noexcept { id_ = id; }

std::size_t Person::id() const noexcept { return id_; }

bool Person::is_alive() const noexcept { return is_alive_; }

bool Person::has_emigrated() const noexcept { return has_emigrated_; }

unsigned int Person::time_of_death() const noexcept { return time_of_death_; }

unsigned int Person::time_of_migration() const noexcept { return time_of_migration_; }

bool Person::is_active() const noexcept { return is_alive_ && !has_emigrated_; }

double Person::get_risk_factor_value(const core::Identifier &key) const {
    const core::Identifier income_id("income");
    if (risk_factors.contains(income_id) &&
        (key == income_id || key == core::Identifier("Income"))) {
        return risk_factors.at(income_id);
    }

    if (current_dispatcher.contains(key)) {
        return current_dispatcher.at(key)(*this);
    }

    if (risk_factors.contains(key)) {
        return risk_factors.at(key);
    }

    throw std::out_of_range("Risk factor not found: " + key.to_string());
}

float Person::gender_to_value() const { return gender_to_value(gender); }

float Person::gender_to_value(core::Gender gender) {
    if (gender == core::Gender::unknown) {
        throw core::InternalError("Gender is unknown.");
    }

    return gender == core::Gender::male ? 1.0f : 0.0f;
}

std::string Person::gender_to_string() const {
    if (gender == core::Gender::unknown) {
        throw core::InternalError("Gender is unknown.");
    }

    return gender == core::Gender::male ? "male" : "female";
}

bool Person::over_18() const noexcept { return age >= 18; }

float Person::sector_to_value() const {
    switch (sector) {
    case core::Sector::urban:
        return 0.0f;
    case core::Sector::rural:
        return 1.0f;
    default:
        throw core::InternalError("Sector is unknown.");
    }
}

float Person::income_to_value() const {
    switch (income) {
    case core::Income::low:
        return 1.0f;
    case core::Income::lowermiddle:
    case core::Income::middle:
        return 2.0f;
    case core::Income::uppermiddle:
        return 3.0f;
    case core::Income::high:
        return 4.0f;
    case core::Income::unknown:
    default:
        throw core::InternalError("Unknown income category");
    }
}

float Person::region_to_value() const {
    if (region == "unknown") {
        throw core::InternalError(
            "Region is unknown - CSV data may not have been loaded properly.");
    }

    if (region.starts_with("region")) {
        try {
            const std::string num_str = region.substr(6);
            return static_cast<float>(std::stoi(num_str));
        } catch (const std::exception &) {
            throw core::InternalError("Invalid region format: " + region);
        }
    }

    throw core::InternalError("Unknown region format: " + region);
}

float Person::ethnicity_to_value() const {
    if (ethnicity == "unknown") {
        throw core::InternalError(
            "Ethnicity is unknown - CSV data may not have been loaded properly.");
    }

    if (ethnicity.starts_with("ethnicity")) {
        try {
            const std::string num_str = ethnicity.substr(9);
            return static_cast<float>(std::stoi(num_str));
        } catch (const std::exception &) {
            throw core::InternalError("Invalid ethnicity format: " + ethnicity);
        }
    }

    throw core::InternalError("Unknown ethnicity format: " + ethnicity);
}

void Person::emigrate(unsigned int time) {
    if (!is_active()) {
        throw std::logic_error("Entity must be active prior to emigrate.");
    }

    has_emigrated_ = true;
    time_of_migration_ = time;
}

void Person::die(unsigned int time) {
    if (!is_active()) {
        throw std::logic_error("Entity must be active prior to death.");
    }

    is_alive_ = false;
    time_of_death_ = time;
}

} // namespace hgps