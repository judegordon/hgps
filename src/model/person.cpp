#include "person.h"

#include "core/chars.h"
#include "diagnostics/internal_error.h"
#include "predictor_resolver.h"

#include <cmath>
#include <functional>

#include <fmt/format.h>

namespace hgps::model {
namespace {

/// The named predictors that are functions of a person's state rather than stored risk factors.
///
/// std::map, and iterated nowhere — it is a lookup table only. The names are the ones the model
/// files use, including the FINCH spellings.
const std::map<core::Identifier, std::function<double(const Person &)>> &dispatcher() {
    static const std::map<core::Identifier, std::function<double(const Person &)>> table{
        {"intercept"_id, [](const Person &) { return 1.0; }},
        {"gender"_id, [](const Person &p) { return static_cast<double>(p.gender_to_value()); }},
        {"age"_id, [](const Person &p) { return static_cast<double>(p.age); }},
        {"age2"_id,
         [](const Person &p) { return static_cast<double>(p.age) * static_cast<double>(p.age); }},
        {"age3"_id,
         [](const Person &p) {
             const auto age = static_cast<double>(p.age);
             return age * age * age;
         }},
        {"over18"_id, [](const Person &p) { return p.over_18() ? 1.0 : 0.0; }},
        {"sector"_id, [](const Person &p) { return static_cast<double>(p.sector_to_value()); }},
        {"region"_id, [](const Person &p) { return static_cast<double>(p.region_to_value()); }},
        {"ethnicity"_id,
         [](const Person &p) { return static_cast<double>(p.ethnicity_to_value()); }},
        {"income"_id, [](const Person &p) { return static_cast<double>(p.income_to_value()); }},
        {"ses"_id, [](const Person &p) { return p.ses; }},

        // FINCH model files use these spellings for the same quantities.
        {"age1"_id, [](const Person &p) { return static_cast<double>(p.age); }},
        {"ethnicity2"_id,
         [](const Person &p) { return p.ethnicity_to_value() == 2.0F ? 1.0 : 0.0; }},
        {"ethnicity3"_id,
         [](const Person &p) { return p.ethnicity_to_value() == 3.0F ? 1.0 : 0.0; }},
        {"ethnicity4"_id,
         [](const Person &p) { return p.ethnicity_to_value() == 4.0F ? 1.0 : 0.0; }},
        {"region2"_id, [](const Person &p) { return p.region_to_value() == 2.0F ? 1.0 : 0.0; }},
        {"region3"_id, [](const Person &p) { return p.region_to_value() == 3.0F ? 1.0 : 0.0; }},
        {"region4"_id, [](const Person &p) { return p.region_to_value() == 4.0F ? 1.0 : 0.0; }},
    };
    return table;
}

/// The trailing digits of a name like "region3" or "ethnicity12".
std::optional<int> trailing_number(const std::string &value, std::size_t prefix_length) {
    if (value.size() <= prefix_length) {
        return std::nullopt;
    }

    int result = 0;
    for (std::size_t i = prefix_length; i < value.size(); ++i) {
        if (!core::chars::is_digit(value[i])) {
            return std::nullopt;
        }
        result = result * 10 + (value[i] - '0');
    }
    return result;
}

} // namespace

std::optional<double> Person::try_risk_factor_value(const core::Identifier &key) const {
    // A stored income value wins, so a continuous-income model works before categories exist.
    static const core::Identifier income_id{"income"};
    if (key == income_id) {
        if (const auto found = risk_factors.find(income_id); found != risk_factors.end()) {
            return found->second;
        }
    }

    // A stored risk factor comes before the named-predictor table, so a model that carries a
    // factor called "income" or "sector" gets its own value rather than the derived one.
    if (const auto found = risk_factors.find(key); found != risk_factors.end()) {
        return found->second;
    }

    const auto &table = dispatcher();
    if (const auto found = table.find(key); found != table.end()) {
        return found->second(*this);
    }

    return resolve_derived_predictor(*this, key.to_string());
}

double Person::get_risk_factor_value(const core::Identifier &key) const {
    if (const auto value = try_risk_factor_value(key)) {
        return *value;
    }

    // Not a user mistake: every coefficient name is validated when its model file is loaded, so
    // an unknown name here means model code asked for something it never registered.
    throw diag::InternalError(
        fmt::format("risk factor or predictor '{}' is not available for this person; every "
                    "predictor name is validated at model-load time, so this is a bug",
                    key.to_string()));
}

float Person::gender_to_value() const { return gender_to_value(gender); }

float Person::gender_to_value(core::Gender gender) {
    if (gender == core::Gender::unknown) {
        throw diag::InternalError("a person's gender is unknown");
    }
    return gender == core::Gender::male ? 1.0F : 0.0F;
}

std::string Person::gender_to_string() const {
    if (gender == core::Gender::unknown) {
        throw diag::InternalError("a person's gender is unknown");
    }
    return gender == core::Gender::male ? "male" : "female";
}

float Person::sector_to_value() const {
    switch (sector) {
    case core::Sector::urban:
        return 0.0F;
    case core::Sector::rural:
        return 1.0F;
    case core::Sector::unknown:
        break;
    }
    throw diag::InternalError("a person's sector is unknown");
}

float Person::income_to_value() const {
    switch (income) {
    case core::Income::low:
        return 1.0F;
    case core::Income::lowermiddle:
    case core::Income::middle:
        // Both middle categories share a value, as upstream, so a model coefficient fitted on the
        // three-category encoding still means what it meant.
        return 2.0F;
    case core::Income::uppermiddle:
        return 3.0F;
    case core::Income::high:
        return 4.0F;
    case core::Income::unknown:
        break;
    }
    throw diag::InternalError("a person's income category is unknown");
}

float Person::region_to_value() const {
    if (region == "unknown") {
        throw diag::InternalError("a person's region is unset; the region model did not run");
    }

    if (const auto number = trailing_number(region, std::string_view{"region"}.size());
        region.starts_with("region") && number.has_value()) {
        return static_cast<float>(*number);
    }

    throw diag::InternalError(
        fmt::format("region '{}' is not of the form region<N>", region));
}

float Person::ethnicity_to_value() const {
    if (ethnicity == "unknown") {
        throw diag::InternalError(
            "a person's ethnicity is unset; the ethnicity model did not run");
    }

    if (const auto number = trailing_number(ethnicity, std::string_view{"ethnicity"}.size());
        ethnicity.starts_with("ethnicity") && number.has_value()) {
        return static_cast<float>(*number);
    }

    throw diag::InternalError(
        fmt::format("ethnicity '{}' is not of the form ethnicity<N>", ethnicity));
}

void Person::emigrate(unsigned int time) {
    if (!is_active()) {
        throw diag::InternalError("a person must be active before emigrating");
    }

    has_emigrated_ = true;
    time_of_migration_ = time;
}

void Person::die(unsigned int time) {
    if (!is_active()) {
        throw diag::InternalError("a person must be active before dying");
    }

    is_alive_ = false;
    time_of_death_ = time;
}

} // namespace hgps::model
