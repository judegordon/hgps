// Baseline compatibility flags: the deliberate deviations, each one switchable.
//
// This build fixes defects in the baseline, and some of those fixes change the numbers
// (docs/deviations.md). A deviation that cannot be switched off is indistinguishable, in a
// comparison, from a mistake: the equivalence harness can only report "these two disagree", not
// "these two disagree by exactly the amount this fix is worth".
//
// So every deviation that changes outputs gets a flag here, named after its entry in
// docs/deviations.md. With the flag on, the engine reproduces the baseline's behaviour exactly —
// bug and all. The flags are off by default, because the fixed behaviour is the one this project
// stands behind; they exist to make the difference measurable
// (docs/decisions/0041-deliberate-deviations-are-switchable.md).
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hgps::api {

/// @brief One deviation that can be put back. The name is the deviation's ID in
///        docs/deviations.md, so a flag in a config or a manifest leads straight to the entry
///        explaining what it restores.
enum class CompatFlag : std::uint8_t {
    /// @brief B-24: the food-labelling policy re-applies its impact to somebody who failed an
    ///        early coverage draw and passed a later one.
    b24 = 0,

    /// @brief B-29: the energy balance integrates a body fat mass straight through zero and past
    ///        the partition coefficient's pole, instead of stopping at the edge of the model's
    ///        domain (docs/findings/seed-80.md).
    b29 = 1,

    /// @brief B-30: a risk factor that is not a number is silently counted as zero when the
    ///        year's means are accumulated, so the mean is wrong and nothing says so.
    b30 = 2,
};

/// @brief A set of compatibility flags. Default-constructed means "none" — the fixed behaviour.
class BaselineCompat {
  public:
    /// @brief How many flags exist. Grows when a deviation does.
    static constexpr std::size_t flag_count = 3;

    BaselineCompat() = default;

    /// @brief Every flag on: the baseline's behaviour throughout. This is what the equivalence
    ///        harness compares with, so that a comparison tests everything *except* the
    ///        deliberate deviations.
    static BaselineCompat all() noexcept;

    bool is_set(CompatFlag flag) const noexcept { return bits_.test(index_of(flag)); }
    void set(CompatFlag flag, bool on = true) noexcept { bits_.set(index_of(flag), on); }

    bool any() const noexcept { return bits_.any(); }
    bool none() const noexcept { return bits_.none(); }
    std::size_t count() const noexcept { return bits_.count(); }

    bool operator==(const BaselineCompat &) const noexcept = default;

    /// @brief The flags that are on, by name, in declaration order. This is what goes into a
    ///        manifest, and an empty vector is a complete answer.
    std::vector<std::string> names() const;

    /// @brief Adds every flag `other` has. Union, because the API's flags and the config's are
    ///        both requests to restore a behaviour and neither overrides the other.
    BaselineCompat &merge(const BaselineCompat &other) noexcept;

    /// @brief The flag a name denotes, or nullopt. Matching ignores case and treats `-` and `_`
    ///        as absent, so "B-24", "b24" and "B_24" are the same flag.
    static std::optional<CompatFlag> from_name(std::string_view name);

    /// @brief `from_name`, extended with "all" — the whole set, whatever it grows to contain.
    ///        Returns nullopt for an unknown name; `*out` is unchanged in that case.
    static bool apply_name(std::string_view name, BaselineCompat &out);

    /// @brief The canonical name, as docs/deviations.md writes it.
    static std::string_view name_of(CompatFlag flag) noexcept;

    /// @brief One line on what switching it on restores.
    static std::string_view description_of(CompatFlag flag) noexcept;

    /// @brief Every flag, in declaration order — for a command line's help, a config schema, and
    ///        the message that lists what a caller could have meant.
    static const std::vector<CompatFlag> &known() noexcept;

    /// @brief Every flag's name, comma-separated, plus "all". For a diagnostic.
    static std::string known_names_sentence();

  private:
    static std::size_t index_of(CompatFlag flag) noexcept {
        return static_cast<std::size_t>(flag);
    }

    std::bitset<flag_count> bits_;
};

} // namespace hgps::api
