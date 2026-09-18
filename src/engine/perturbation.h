// A deliberate corruption of a run's output, for the equivalence harness's own tests.
//
// The harness decides this project's headline result, so a mistake in it says PASS. It has 26 tests
// of its own statistics, and two of its rules have been wrong once each. What those tests cannot
// answer is whether the whole thing, run end to end against real output, notices a difference that
// is really there — and the only way to ask is to put a difference there on purpose
// (docs/equivalence-method.md, ADR 0036).
//
// This is the knob that does it. It is off in every ordinary run, it is refused unless every channel
// it names exists, and every run manifest records whatever it was set to — so a perturbed run cannot
// be mistaken for a real one by anybody who reads the manifest, which is what a manifest is for.
#pragma once

#include "model/results.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace hgps::engine {

/// @brief One channel's corruption.
struct PerturbationRule {
    enum class Operation {
        /// @brief Multiply every age band's value by `value`.
        scale,
        /// @brief Add `value` to exactly one age band — the lowest with anybody in it.
        ///
        /// For a counted channel this is one lattice step of the reduced series, which is what the
        /// harness's lattice rule is about. Applying it to one band rather than all of them is the
        /// point: the reduction sums counts over bands, so one band is one step.
        step,
    };

    std::string channel;
    Operation operation{Operation::scale};
    double value{1.0};
};

/// @brief Applies a set of rules to each year's result before it reaches the writer.
class Perturbation {
  public:
    Perturbation() = default;

    /// @brief Parses a specification: `channel=op:value`, separated by `;`.
    ///
    /// Example: `mean_bmi=scale:1.01;mean_energy=scale:1.05;emigrations=step:1`.
    ///
    /// @return nullopt and a message in `error` for anything it cannot parse. A typo must not
    ///         silently produce an unperturbed run, because the test that uses this asserts a
    ///         failure and would then pass for the wrong reason.
    static std::optional<Perturbation> parse(const std::string &specification,
                                             std::string &error);

    bool empty() const noexcept { return rules_.empty(); }
    const std::string &specification() const noexcept { return specification_; }
    const std::vector<PerturbationRule> &rules() const noexcept { return rules_; }

    /// @brief The channels the rules name.
    std::vector<std::string> channels() const;

    /// @brief Corrupts one year of one scenario, in place.
    void apply(model::ModelResult &result);

    /// @brief The channels this perturbation named and never found in the output.
    ///
    /// The output's channel set is decided by what the loaded models actually assign, so it is not
    /// known until the first year has been analysed — which is why this is checked *after* the run
    /// rather than before it. A rule that never fired is a typo, and a typo that produced an
    /// unperturbed run would make the test that uses this pass for the wrong reason, which is the
    /// single worst outcome available here.
    std::vector<std::string> rules_that_never_fired() const;

  private:
    std::string specification_;
    std::vector<PerturbationRule> rules_;

    /// @brief How many times each rule has been applied, parallel to `rules_`.
    std::vector<std::size_t> applied_;
};

} // namespace hgps::engine
