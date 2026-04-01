#pragma once
#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <unordered_map>

#include "nlohmann/json.hpp"

namespace hgps {
namespace core {

struct Identifier final {
    constexpr Identifier() = default;

    Identifier(const char *const value);

    Identifier(const std::string &value);

    bool is_empty() const noexcept;

    std::size_t size() const noexcept;

    const std::string &to_string() const noexcept;

    std::size_t hash() const noexcept;

    bool equal(const std::string &other) const noexcept;

    bool equal(const Identifier &other) const noexcept;

    bool operator==(const Identifier &rhs) const noexcept;

    std::strong_ordering operator<=>(const Identifier &rhs) const noexcept = default;

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
void map_from_json(const nlohmann::json &j, Map<hgps::core::Identifier, Value> &map) {
    map.clear();
    for (const auto &item : j.items()) {
        map.emplace(item.key(), item.value().get<Value>());
    }
}
} // namespace detail

} // namespace core

core::Identifier operator""_id(const char *id, size_t);
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
