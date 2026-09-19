#include "hgps/baseline_compat.h"

#include <algorithm>
#include <array>

#include <fmt/format.h>

namespace hgps::api {
namespace {

struct FlagInfo {
    CompatFlag flag;
    std::string_view name;
    std::string_view description;
};

// One row per flag. The name is the deviation's ID in docs/deviations.md; a test checks that every
// flag here has an entry there, so a flag cannot be added without explaining what it restores.
constexpr std::array<FlagInfo, BaselineCompat::flag_count> kFlags{{
    {CompatFlag::b24, "B-24",
     "the food-labelling policy offers its impact again, every remaining year of the coverage "
     "window, to somebody who failed an early coverage draw and passed a later one"},
    {CompatFlag::b29, "B-29",
     "the energy balance lets a body fat mass go through zero and past the pole of the partition "
     "coefficient, so a starved draw runs away to an impossible weight instead of stopping at "
     "the edge of the model's domain"},
    {CompatFlag::b30, "B-30",
     "a risk factor that is not a number is counted as zero while the year's means are "
     "accumulated, rather than stopping the run, so an impossible value reaches the results "
     "file as a quietly wrong mean"},
}};

/// @brief Name matching that ignores case and the separators people write between a letter and a
///        number. "B-24", "b24" and "B_24" are one flag; "B-2 4" is not.
std::string canonical(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        if (c == '-' || c == '_') {
            continue;
        }
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

BaselineCompat BaselineCompat::all() noexcept {
    BaselineCompat compat;
    compat.bits_.set();
    return compat;
}

std::vector<std::string> BaselineCompat::names() const {
    std::vector<std::string> out;
    for (const auto &info : kFlags) {
        if (is_set(info.flag)) {
            out.emplace_back(info.name);
        }
    }
    return out;
}

BaselineCompat &BaselineCompat::merge(const BaselineCompat &other) noexcept {
    bits_ |= other.bits_;
    return *this;
}

std::optional<CompatFlag> BaselineCompat::from_name(std::string_view name) {
    const auto wanted = canonical(name);
    for (const auto &info : kFlags) {
        if (canonical(info.name) == wanted) {
            return info.flag;
        }
    }
    return std::nullopt;
}

bool BaselineCompat::apply_name(std::string_view name, BaselineCompat &out) {
    if (canonical(name) == "all") {
        out.merge(all());
        return true;
    }
    const auto flag = from_name(name);
    if (!flag.has_value()) {
        return false;
    }
    out.set(*flag);
    return true;
}

std::string_view BaselineCompat::name_of(CompatFlag flag) noexcept {
    return kFlags.at(static_cast<std::size_t>(flag)).name;
}

std::string_view BaselineCompat::description_of(CompatFlag flag) noexcept {
    return kFlags.at(static_cast<std::size_t>(flag)).description;
}

const std::vector<CompatFlag> &BaselineCompat::known() noexcept {
    static const std::vector<CompatFlag> all_flags = [] {
        std::vector<CompatFlag> out;
        out.reserve(kFlags.size());
        for (const auto &info : kFlags) {
            out.push_back(info.flag);
        }
        return out;
    }();
    return all_flags;
}

std::string BaselineCompat::known_names_sentence() {
    std::string out;
    for (const auto &info : kFlags) {
        if (!out.empty()) {
            out += ", ";
        }
        out += info.name;
    }
    if (!out.empty()) {
        out += ", ";
    }
    out += "all";
    return out;
}

} // namespace hgps::api
