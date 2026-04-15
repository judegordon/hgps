#pragma once

#include <compare>
#include <cstddef>
#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <unordered_map>

#include "nlohmann/json.hpp"

namespace hgps::core {

struct Identifier final {
    constexpr Identifier() = default;

    Identifier(const char* value);

    Identifier(const std::string& value);

    bool is_empty() const noexcept;

    std::size_t size() const noexcept;

    const std::string& to_string() const noexcept;

    std::size_t hash() const noexcept;

    bool equal(const std::string& other) const noexcept;

    bool equal(const Identifier& other) const noexcept;

    bool operator==(const Identifier& rhs) const noexcept;

    std::strong_ordering operator<=>(const Identifier& rhs) const noexcept = default;

    static Identifier empty();

    friend std::ostream& operator<<(std::ostream& stream, const Identifier& identifier);

  private:
    std::string value_{};
    std::size_t hash_code_{std::hash<std::string>{}("")};

    void validate_identifier() const;
};

void from_json(const nlohmann::json& j, Identifier& id);

template <class T>
void from_json(const nlohmann::json& j, std::map<Identifier, T>& map) {
    map.clear();
    for (const auto& item : j.items()) {
        map.emplace(Identifier{item.key()}, item.value().template get<T>());
    }
}

template <class T>
void from_json(const nlohmann::json& j, std::unordered_map<Identifier, T>& map) {
    map.clear();
    for (const auto& item : j.items()) {
        map.emplace(Identifier{item.key()}, item.value().template get<T>());
    }
}

Identifier operator""_id(const char* id, std::size_t);

} // namespace hgps::core

namespace std {

template <>
struct hash<hgps::core::Identifier> {
    std::size_t operator()(const hgps::core::Identifier& id) const noexcept { return id.hash(); }
};

} // namespace std