// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/person.h, person.cpp.
#pragma once

#include "core/identifier.h"
#include "core/types.h"
#include "factor_values.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace hgps::model {

enum class DiseaseStatus : std::uint8_t { free, active };

/// @brief One disease's state for one person.
struct Disease {
    DiseaseStatus status{};
    int start_time{};

    /// @brief Years since onset. Cancer models only; -1 means "not applicable".
    int time_since_onset{-1};

    Disease clone() const noexcept { return *this; }
};

/// @brief One simulated person.
///
/// The identifier is assigned by Population and is unique for the whole run: a storage slot may
/// be reused by a newborn, an identifier never is. The earlier rewrite made identifiers
/// slot-based, which silently conflates a dead person with their successor in the per-person
/// tracking output (audit R-03, docs/decisions/0017-person-ids-monotonic-and-free-slots.md).
class Person {
  public:
    /// @brief The value an identifier has before Population assigns one.
    static constexpr std::size_t unassigned_id = 0;

    Person() = default;
    // The parameter is not named `gender`: GCC's -Wshadow rejects a constructor parameter that
    // shadows a member, where clang needs -Wshadow-field-in-constructor to say the same thing.
    // Both are on now (cmake/warnings.cmake).
    explicit Person(core::Gender initial_gender) noexcept : gender{initial_gender} {}
    explicit Person(std::size_t id) noexcept : id_{id} {}
    Person(core::Gender initial_gender, std::size_t id) noexcept
        : gender{initial_gender}, id_{id} {}

    std::size_t id() const noexcept { return id_; }
    bool has_assigned_id() const noexcept { return id_ != unassigned_id; }

    /// @brief Assigns the identifier. Population's business; never called from a model.
    void set_id(std::size_t id) noexcept { id_ = id; }

    core::Gender gender{core::Gender::unknown};
    unsigned int age{};
    core::Sector sector{core::Sector::unknown};

    /// @brief Region as named by the model's region CSV, e.g. "region1".
    std::string region{"unknown"};

    /// @brief Ethnicity as named by the model's ethnicity CSV, e.g. "ethnicity1".
    std::string ethnicity{"unknown"};

    core::Income income{core::Income::unknown};
    double income_continuous{0.0};

    /// @brief The rank bucket used for stratum-specific FactorsMean adjustment, if any. Separate
    ///        from `income`, which is the reported category.
    std::size_t income_adjustment_stratum{0};
    bool has_income_adjustment_stratum{false};

    double physical_activity{0.0};
    double ses{};

    /// @brief Risk factor values, keyed by index.
    ///
    /// A flat store keyed by a run-wide name-to-index table rather than a `std::map`: every risk-factor
    /// read on every person in every year used to be a tree of string comparisons, and that was 40% of
    /// HLM_France's samples and 52% of KevinHall_FINCH's
    /// ([ADR 0037](../../docs/decisions/0037-index-keyed-risk-factor-store.md)).
    ///
    /// It presents a map's surface, with one difference: **iteration is in index order, not name
    /// order**. Exactly one place in the tree multiplies over a person's factors, where the order is
    /// part of the result, and it iterates its own name-ordered list instead.
    FactorValues risk_factors;

    /// @brief Disease state, ordered by disease code.
    std::map<core::Identifier, Disease> diseases;

    bool is_alive() const noexcept { return is_alive_; }
    bool has_emigrated() const noexcept { return has_emigrated_; }
    unsigned int time_of_death() const noexcept { return time_of_death_; }
    unsigned int time_of_migration() const noexcept { return time_of_migration_; }

    /// @brief Alive and not emigrated.
    bool is_active() const noexcept { return is_alive_ && !has_emigrated_; }

    bool over_18() const noexcept { return age >= 18; }

    /// @brief A risk factor or derived predictor value.
    ///
    /// Resolution order: a stored `income` override, the derived-predictor table, this person's
    /// risk factors, then the derived-predictor resolver.
    ///
    /// @throws diag::InternalError for an unknown name. Every coefficient name in every model
    ///         file is checked against the registered factor set at load time
    ///         (docs/decisions/0018-no-swallowing-catch-load-time-validation.md), so reaching
    ///         here means a programmer error rather than a user mistake.
    double get_risk_factor_value(const core::Identifier &key) const;

    /// @brief The same lookup, without throwing: nullopt when the name resolves to nothing.
    ///
    /// The non-throwing form is the primitive, because both the linear model evaluator and the
    /// `log_<name>` predictor need "is this resolvable?" rather than "give me it or fail".
    std::optional<double> try_risk_factor_value(const core::Identifier &key) const;

    /// @brief 1 for male, 0 for female.
    /// @throws diag::InternalError if the gender is unknown.
    float gender_to_value() const;
    static float gender_to_value(core::Gender gender);

    /// @throws diag::InternalError if the gender is unknown.
    std::string gender_to_string() const;

    /// @brief 0 for urban, 1 for rural.
    /// @throws diag::InternalError if the sector is unknown.
    float sector_to_value() const;

    /// @brief The income encoding the models use: low 1, either middle 2, upper-middle 3, high 4.
    /// @throws diag::InternalError if the income category is unknown.
    float income_to_value() const;

    /// @brief The trailing number of the region name, e.g. "region3" gives 3.
    /// @throws diag::InternalError if the region is unset or not in that form.
    float region_to_value() const;

    /// @brief The trailing number of the ethnicity name.
    /// @throws diag::InternalError if the ethnicity is unset or not in that form.
    float ethnicity_to_value() const;

    /// @throws diag::InternalError if the person is not active.
    void emigrate(unsigned int time);

    /// @throws diag::InternalError if the person is not active.
    void die(unsigned int time);

  private:
    std::size_t id_{unassigned_id};
    bool is_alive_{true};
    bool has_emigrated_{false};
    unsigned int time_of_death_{};
    unsigned int time_of_migration_{};
};

} // namespace hgps::model
