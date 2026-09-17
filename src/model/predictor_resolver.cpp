#include "predictor_resolver.h"

#include "core/chars.h"
#include "core/string_util.h"
#include "diagnostics/internal_error.h"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

namespace hgps::model {
namespace {

/// log(0) is not a number a risk factor can carry, so the log predictors floor their argument.
constexpr double kLogFloor = 1e-10;

/// "age3" -> 3; "age" -> 1; "agex" -> nullopt.
std::optional<int> trailing_power(const std::string &name, std::string_view prefix) {
    if (!name.starts_with(prefix)) {
        return std::nullopt;
    }
    if (name.size() == prefix.size()) {
        return 1;
    }

    int power = 0;
    for (std::size_t i = prefix.size(); i < name.size(); ++i) {
        if (!core::chars::is_digit(name[i])) {
            return std::nullopt;
        }
        power = power * 10 + (name[i] - '0');
    }
    return power;
}

double income_base_value(const Person &person) {
    static const core::Identifier income_id{"income"};
    if (const auto found = person.risk_factors.find(income_id);
        found != person.risk_factors.end()) {
        return found->second;
    }
    if (person.income_continuous > 0.0) {
        return person.income_continuous;
    }
    return static_cast<double>(person.income_to_value());
}

std::optional<double> resolve_log(const Person &person, const std::string &key) {
    if (!key.starts_with("log_")) {
        return std::nullopt;
    }

    const auto remainder = key.substr(4);
    if (remainder.empty()) {
        return std::nullopt;
    }

    // Trailing digits are a power on the log, not on the base: log_income2 is (log income)².
    std::size_t base_end = remainder.size();
    while (base_end > 0 && core::chars::is_digit(remainder[base_end - 1])) {
        --base_end;
    }

    int log_power = 1;
    if (base_end < remainder.size()) {
        log_power = 0;
        for (std::size_t i = base_end; i < remainder.size(); ++i) {
            log_power = log_power * 10 + (remainder[i] - '0');
        }
    }

    const auto base_name = remainder.substr(0, base_end);
    if (base_name.empty()) {
        return std::nullopt;
    }

    // Through the person's full lookup, not just the derived table: log_energy names a stored
    // risk factor, which is the commonest shape in the FINCH model files.
    const auto base_value = person.try_risk_factor_value(core::Identifier{base_name});
    if (!base_value.has_value()) {
        return std::nullopt;
    }

    return std::pow(std::log(std::max(*base_value, kLogFloor)), log_power);
}

std::optional<double> resolve_age(const Person &person, const std::string &key) {
    const auto lower = core::to_lower(key);
    if (const auto power = trailing_power(lower, "age")) {
        return std::pow(static_cast<double>(person.age), *power);
    }
    return std::nullopt;
}

std::optional<double> resolve_income(const Person &person, const std::string &key) {
    if (key == "income_continuous") {
        return person.income_continuous;
    }
    if (const auto power = trailing_power(core::to_lower(key), "income")) {
        return std::pow(income_base_value(person), *power);
    }
    return std::nullopt;
}

std::optional<double> resolve_gender(const Person &person, const std::string &key) {
    if (is_gender2_predictor(key)) {
        // The gender2 row needs the configured indicator sex, which only the model evaluator
        // knows, so it is resolved there rather than here.
        return std::nullopt;
    }

    const auto lower = core::to_lower(key);
    if (lower == "gender") {
        return static_cast<double>(person.gender_to_value());
    }
    if (const auto power = trailing_power(lower, "gender")) {
        const double dummy = person.gender == core::Gender::male ? 1.0 : 0.0;
        return std::pow(dummy, *power);
    }
    return std::nullopt;
}

std::optional<double> resolve_sector(const Person &person, const std::string &key) {
    if (const auto power = trailing_power(core::to_lower(key), "sector")) {
        return std::pow(static_cast<double>(person.sector_to_value()), *power);
    }
    return std::nullopt;
}

std::optional<double> resolve_energy_intake(const Person &person, const std::string &key) {
    if (!core::case_insensitive::equals(key, "energyintake")) {
        return std::nullopt;
    }

    static const core::Identifier energy_intake{"energyintake"};
    if (const auto found = person.risk_factors.find(energy_intake);
        found != person.risk_factors.end()) {
        return found->second;
    }
    return std::nullopt;
}

/// A region or ethnicity dummy: 1 when the person is in that category, otherwise 0. A bare
/// "region" or "ethnicity" is the numeric value instead.
double category_dummy(const std::string &person_category, const std::string &key,
                      float numeric_value) {
    if (key == "region" || key == "ethnicity") {
        return static_cast<double>(numeric_value);
    }
    return core::case_insensitive::equals(person_category, key) ? 1.0 : 0.0;
}

std::size_t edit_distance(const std::string &left, const std::string &right) {
    std::vector<std::size_t> previous(right.size() + 1);
    std::vector<std::size_t> current(right.size() + 1);
    for (std::size_t j = 0; j <= right.size(); ++j) {
        previous[j] = j;
    }

    for (std::size_t i = 1; i <= left.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= right.size(); ++j) {
            const auto cost = left[i - 1] == right[j - 1] ? 0U : 1U;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        previous = current;
    }

    return previous[right.size()];
}

} // namespace

bool is_gender2_predictor(const std::string &key) {
    return core::case_insensitive::equals(key, "gender2");
}

double gender2_regression_value(const Person &person, core::Gender indicator_sex) {
    return person.gender == indicator_sex ? 1.0 : 0.0;
}

core::Gender parse_gender2_indicator(const std::string &indicator_label) {
    const auto lower = core::to_lower(indicator_label);
    if (lower == "male") {
        return core::Gender::male;
    }
    if (lower == "female") {
        return core::Gender::female;
    }

    throw diag::InternalError(
        fmt::format("project_requirements.demographics.gender2 must be \"male\" or \"female\", "
                    "got: {}",
                    indicator_label));
}

bool is_metadata_predictor(const std::string &name) {
    return core::case_insensitive::equals(name, "stddev") ||
           core::case_insensitive::equals(name, "min") ||
           core::case_insensitive::equals(name, "max") ||
           core::case_insensitive::equals(name, "lambda") ||
           core::case_insensitive::equals(name, "intercept");
}

bool is_metadata_predictor(const core::Identifier &name) {
    return is_metadata_predictor(name.to_string());
}

std::optional<double> resolve_derived_predictor(const Person &person, const std::string &key) {
    if (key.empty() || is_metadata_predictor(key)) {
        return std::nullopt;
    }

    if (const auto value = resolve_log(person, key)) {
        return value;
    }
    if (const auto value = resolve_income(person, key)) {
        return value;
    }

    // A name that IS this person's category, e.g. "region3" for someone in region3.
    if (core::case_insensitive::equals(key, person.region) ||
        core::case_insensitive::equals(key, person.ethnicity)) {
        return 1.0;
    }

    if (const auto value = resolve_age(person, key)) {
        return value;
    }
    if (const auto value = resolve_gender(person, key)) {
        return value;
    }

    if (key.starts_with("region")) {
        return category_dummy(person.region, key, person.region == "unknown"
                                                      ? 0.0F
                                                      : person.region_to_value());
    }
    if (key.starts_with("ethnicity")) {
        return category_dummy(person.ethnicity, key,
                              person.ethnicity == "unknown" ? 0.0F : person.ethnicity_to_value());
    }

    if (const auto value = resolve_sector(person, key)) {
        return value;
    }

    return resolve_energy_intake(person, key);
}

bool is_resolvable_predictor(const std::string &key,
                             const std::vector<core::Identifier> &known_factors) {
    if (key.empty()) {
        return false;
    }
    if (is_metadata_predictor(key) || is_gender2_predictor(key)) {
        return true;
    }

    const auto lower = core::to_lower(key);
    for (const auto &factor : known_factors) {
        if (factor.to_string() == lower) {
            return true;
        }
    }

    // A structural check against a probe person, so the answer is "is this name resolvable at
    // all" rather than "does it resolve for this particular person". The probe is given a region
    // and an ethnicity so the category dummies resolve, and a sector and income so those do too.
    // The probe's full lookup is used, so a named predictor like `ses` and a logged risk factor
    // like `log_energy` are both recognised.
    Person probe{core::Gender::male, 1};
    probe.age = 40;
    probe.region = "region1";
    probe.ethnicity = "ethnicity1";
    probe.sector = core::Sector::urban;
    probe.income = core::Income::low;
    probe.income_continuous = 1.0;
    for (const auto &factor : known_factors) {
        probe.risk_factors[factor] = 1.0;
    }

    return probe.try_risk_factor_value(core::Identifier{core::to_lower(key)}).has_value();
}

std::vector<std::string> nearest_predictor_names(const std::string &key,
                                                 const std::vector<core::Identifier> &known_factors,
                                                 std::size_t limit) {
    const auto lower = core::to_lower(key);

    std::vector<std::pair<std::size_t, std::string>> scored;
    scored.reserve(known_factors.size());
    for (const auto &factor : known_factors) {
        scored.emplace_back(edit_distance(lower, factor.to_string()), factor.to_string());
    }

    // A few derived names are worth suggesting too, because they are the ones people mistype.
    for (const auto *candidate : {"age", "age2", "age3", "gender", "gender2", "income",
                                  "income_continuous", "ses", "sector", "region", "ethnicity",
                                  "intercept"}) {
        scored.emplace_back(edit_distance(lower, candidate), candidate);
    }

    std::sort(scored.begin(), scored.end());

    std::vector<std::string> result;
    for (const auto &[distance, name] : scored) {
        // Anything further away than a third of the name's length is not a suggestion, it is
        // noise.
        if (distance > std::max<std::size_t>(2, lower.size() / 3)) {
            break;
        }
        if (result.size() >= limit) {
            break;
        }
        result.push_back(name);
    }

    return result;
}

} // namespace hgps::model
