#include "person.h"

#include "core/chars.h"
#include "diagnostics/internal_error.h"
#include "predictor_resolver.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

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

/// The dispatcher again, keyed by risk-factor index instead of by name.
///
/// This resolution is the hottest path in the program — once per coefficient per person per year —
/// and after the store became index-keyed it was what was left: a `std::map<Identifier, …>` tree walk
/// for every derived predictor, which is most of them, on top of a hash probe for the stored ones
/// (ADR 0037). So each of the nineteen names is interned once, here, and the lookup becomes a scan of
/// nineteen 32-bit integers.
///
/// Built on first use from `dispatcher()`, so there is one list of names and one place to add to.
const std::vector<std::pair<std::uint32_t, const std::function<double(const Person &)> *>> &
indexed_dispatcher() {
    static const auto table = [] {
        std::vector<std::pair<std::uint32_t, const std::function<double(const Person &)> *>> result;
        result.reserve(dispatcher().size());
        for (const auto &[name, function] : dispatcher()) {
            result.emplace_back(factor_index().intern(name), &function);
        }
        return result;
    }();
    return table;
}

void intern_derived_predictors() { indexed_dispatcher(); }

std::optional<double> Person::try_risk_factor_value(const core::Identifier &key) const {
    // Asked for first, and the order is load-bearing: building it interns the nineteen predictor
    // names, and the index lookup below treats a name it has never seen as resolvable only by the
    // derived-predictor resolver. Asking for the index before the table was built therefore sent
    // `age` — on the very first call of a run, before anything else had interned it — straight past
    // the dispatcher. `HLM_France` changed by a last bit and `KevinHall_FINCH` did not, because the
    // window depends on which name a run happens to resolve first. The byte-for-byte comparison in
    // docs/performance.md is what found it.
    indexed_dispatcher();

    // The name is resolved to an index and everything below compares integers.
    return try_risk_factor_value(factor_index().find(key), key);
}

std::optional<double> Person::try_risk_factor_value(std::uint32_t index,
                                                    const core::Identifier &key) const {
    // The hot form: the index is the caller's, resolved once when its model was built, so this
    // whole function is integer comparisons until the fallback. The name-taking overload above is
    // this function with the one hash probe in front of it, which is the entire difference between
    // them — there is no second implementation to keep in step.
    const auto &table = indexed_dispatcher();

    // A name this process has never interned cannot be a stored factor and cannot be one of the
    // nineteen derived predictors, so it goes straight to the resolver — which is also the fast
    // answer for the `log_<name>` shapes, whose names are never interned at all.
    if (index == FactorIndex::unknown) {
        return resolve_derived_predictor(*this, key.to_string());
    }

    // A stored risk factor comes before the named-predictor table, so a model that carries a factor
    // called `income` or `sector` gets its own value rather than the derived one — which is what
    // makes a continuous-income model work before income categories exist.
    //
    // This was two lookups until now: an `income`-specific one and then the general one. They were
    // the same lookup written twice — the first asked whether `income` was stored, which is what the
    // second asks whenever the key *is* `income` — so it is one lookup here, with no change in
    // behaviour for any name.
    if (const auto *value = risk_factors.find_index(index)) {
        return *value;
    }

    for (const auto &[predictor, function] : table) {
        if (predictor == index) {
            return (*function)(*this);
        }
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
