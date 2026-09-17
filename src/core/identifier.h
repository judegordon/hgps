// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS.Core/identifier.h, identifier.cpp.
#pragma once

#include <compare>
#include <cstddef>
#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace hgps::core {

/// @brief A lower-cased entity name used as a key for risk factors, diseases and columns.
///
/// Identifiers must not start with a digit and may contain only [a-z0-9_]; construction
/// lower-cases the value, so "BMI" and "bmi" are the same identifier.
///
/// Equality and ordering both compare the string. The baseline compared only the cached hash in
/// operator== while its defaulted operator<=> compared the string, so `a == b && a < b` could both
/// hold — audit finding B-04. The hash here exists solely as std::hash<Identifier>, for
/// unordered_map bucketing, where collisions are the container's problem and are handled correctly.
/// See docs/decisions/0025-identifier-string-equality.md.
struct Identifier final {
    Identifier() = default;

    /// @throws std::invalid_argument if the value starts with a digit or holds an invalid character.
    Identifier(const char *value);

    /// @throws std::invalid_argument if the value starts with a digit or holds an invalid character.
    Identifier(const std::string &value);

    bool is_empty() const noexcept;
    std::size_t size() const noexcept;
    const std::string &to_string() const noexcept;

    /// @brief The cached hash, for std::hash<Identifier> only. Never for equality.
    std::size_t hash() const noexcept;

    /// @brief Case-insensitive comparison against a raw string.
    bool equal(const std::string &other) const noexcept;
    bool equal(const Identifier &other) const noexcept;

    bool operator==(const Identifier &rhs) const noexcept { return value_ == rhs.value_; }
    std::strong_ordering operator<=>(const Identifier &rhs) const noexcept {
        return value_ <=> rhs.value_;
    }

    static Identifier empty();

    friend std::ostream &operator<<(std::ostream &stream, const Identifier &identifier);

  private:
    std::string value_{};
    std::size_t hash_code_{std::hash<std::string>{}("")};

    void validate_identifier() const;
};

void from_json(const nlohmann::json &j, Identifier &id);

namespace detail {
template <template <typename...> class Map, class Value>
void map_from_json(const nlohmann::json &j, Map<Identifier, Value> &map) {
    map.clear();
    for (const auto &item : j.items()) {
        map.emplace(item.key(), item.value().template get<Value>());
    }
}
} // namespace detail

} // namespace hgps::core

namespace hgps {
/// @brief "bmi"_id
core::Identifier operator""_id(const char *id, std::size_t);
} // namespace hgps

namespace std {

template <class T>
void from_json(const nlohmann::json &j, std::map<hgps::core::Identifier, T> &map) {
    hgps::core::detail::map_from_json(j, map);
}

template <class T>
void from_json(const nlohmann::json &j, std::unordered_map<hgps::core::Identifier, T> &map) {
    hgps::core::detail::map_from_json(j, map);
}

template <> struct hash<hgps::core::Identifier> {
    size_t operator()(const hgps::core::Identifier &id) const noexcept { return id.hash(); }
};

} // namespace std
