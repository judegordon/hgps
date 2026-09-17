#include "identifier.h"

#include "chars.h"
#include "string_util.h"

#include <algorithm>
#include <stdexcept>

namespace hgps::core {

Identifier Identifier::empty() { return Identifier{}; }

Identifier::Identifier(const std::string &value) : value_{to_lower(value)} {
    if (!value_.empty()) {
        validate_identifier();
    }

    hash_code_ = std::hash<std::string>{}(value_);
}

Identifier::Identifier(const char *value) : Identifier{std::string(value)} {}

bool Identifier::is_empty() const noexcept { return value_.empty(); }

std::size_t Identifier::size() const noexcept { return value_.size(); }

const std::string &Identifier::to_string() const noexcept { return value_; }

std::size_t Identifier::hash() const noexcept { return hash_code_; }

bool Identifier::equal(const std::string &other) const noexcept {
    return value_.size() == other.size() && value_ == to_lower(other);
}

bool Identifier::equal(const Identifier &other) const noexcept { return value_ == other.value_; }

void Identifier::validate_identifier() const {
    if (chars::is_digit(value_.front())) {
        throw std::invalid_argument("Identifier must not start with a numeric value");
    }

    if (!std::all_of(value_.begin(), value_.end(),
                     [](char c) { return chars::is_alnum(c) || c == '_'; })) {
        throw std::invalid_argument("Identifier must contain only valid characters: [a-z_0-9]");
    }
}

std::ostream &operator<<(std::ostream &stream, const Identifier &identifier) {
    stream << identifier.to_string();
    return stream;
}

void from_json(const nlohmann::json &j, Identifier &id) { id = Identifier{j.get<std::string>()}; }

} // namespace hgps::core

namespace hgps {
core::Identifier operator""_id(const char *id, std::size_t) { return core::Identifier{id}; }
} // namespace hgps
