// The five age-banded intervention scenarios.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/{marketing_scenario, marketing_dynamic_scenario, fiscal_scenario,
//         physical_activity_scenario, food_labelling_scenario}.cpp, which are five classes with
//         one shared shape written out five times. The shape is factored here into
//         BandedInterventionScenario and each policy supplies only its own exposure rule.
#include "scenario.h"

#include "core/string_util.h"
#include "diagnostics/internal_error.h"

#include <algorithm>
#include <utility>

#include <fmt/format.h>

namespace hgps::sim {
namespace {

using core::Identifier;

} // namespace

BandedInterventionScenario::BandedInterventionScenario(config::InterventionSpec definition,
                                                        std::size_t required_bands)
    : definition_{std::move(definition)} {
    if (definition_.impacts.size() < required_bands) {
        throw diag::InternalError(
            fmt::format("the '{}' intervention needs at least {} impact band(s), and has {}",
                        definition_.identifier, required_bands, definition_.impacts.size()));
    }

    // Ordered and non-overlapping, because `band_of` takes the first band that contains an age and
    // the difference between consecutive bands is what a person gets when they move up. Bands in
    // the wrong order would silently give the wrong difference.
    unsigned int previous_end = 0;
    for (std::size_t i = 0; i < definition_.impacts.size(); ++i) {
        const auto &impact = definition_.impacts[i];
        if (i > 0 && impact.from_age < previous_end) {
            throw diag::InternalError(fmt::format(
                "the '{}' intervention's impact bands overlap or are out of order: band {} starts "
                "at age {}, and the band before it reaches age {}",
                definition_.identifier, i, impact.from_age, previous_end - 1));
        }
        if (impact.to_age.has_value() && *impact.to_age < impact.from_age) {
            throw diag::InternalError(
                fmt::format("the '{}' intervention's impact band {} ends at age {}, before it "
                            "starts at {}",
                            definition_.identifier, i, *impact.to_age, impact.from_age));
        }

        previous_end = impact.to_age.has_value() ? *impact.to_age + 1 : impact.from_age + 1;
        factors_.emplace(Identifier{impact.risk_factor});
    }
}

bool BandedInterventionScenario::is_active(int time) const noexcept {
    if (time < definition_.active_period.start_time) {
        return false;
    }
    return !definition_.active_period.finish_time.has_value() ||
           time <= *definition_.active_period.finish_time;
}

bool BandedInterventionScenario::affects(const Identifier &risk_factor_key) const noexcept {
    return factors_.contains(risk_factor_key);
}

int BandedInterventionScenario::band_of(unsigned int age) const noexcept {
    for (std::size_t i = 0; i < definition_.impacts.size(); ++i) {
        const auto &impact = definition_.impacts[i];
        if (age < impact.from_age) {
            continue;
        }
        if (impact.to_age.has_value() && age > *impact.to_age) {
            continue;
        }
        return static_cast<int>(i);
    }
    return kNeverExposed;
}

double BandedInterventionScenario::impact_value(int index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= definition_.impacts.size()) {
        return 0.0;
    }
    return definition_.impacts[static_cast<std::size_t>(index)].impact_value;
}

double BandedInterventionScenario::apply(rng::RandomSource &random, model::Person &person,
                                          int time, const Identifier &risk_factor_key,
                                          double value) {
    if (!is_active(time) || !affects(risk_factor_key)) {
        return value;
    }
    if (person.age < definition_.impacts.front().from_age) {
        return value;
    }

    return impact_for(random, person, time, risk_factor_key, value);
}

// --- marketing --------------------------------------------------------------------------------

MarketingScenario::MarketingScenario(config::InterventionSpec definition)
    : BandedInterventionScenario{std::move(definition), 3} {}

double MarketingScenario::impact_for(rng::RandomSource & /*random*/, model::Person &person,
                                      int /*time*/, const Identifier & /*risk_factor_key*/,
                                      double value) {
    const int band = band_of(person.age);
    if (band < 0) {
        return value;
    }

    const auto seen = book_.find(person.id());
    if (seen == book_.end()) {
        // Never exposed: the whole of this band's impact.
        book_.emplace(person.id(), band);
        return value + impact_value(band);
    }

    if (seen->second == band) {
        // Already had this band's impact; a policy does not compound year on year.
        return value;
    }

    // Moved up a band: only the difference, so a person who was exposed as a child and is now a
    // teenager ends on the teenage effect rather than on the sum of the two.
    const double difference = impact_value(band) - impact_value(seen->second);
    seen->second = band;
    return value + difference;
}

// --- dynamic marketing ------------------------------------------------------------------------

DynamicMarketingScenario::DynamicMarketingScenario(config::InterventionSpec definition)
    : BandedInterventionScenario{std::move(definition), 1} {
    const auto &dynamics = this->definition().dynamics;
    if (dynamics.size() != 3) {
        throw diag::InternalError(
            fmt::format("'dynamic_marketing' needs exactly three dynamic parameters "
                        "[alpha, beta, gamma], and has {}",
                        dynamics.size()));
    }
    for (const double parameter : dynamics) {
        if (parameter < 0.0 || parameter > 1.0) {
            throw diag::InternalError(fmt::format(
                "a 'dynamic_marketing' parameter must be a probability in [0, 1], not {}",
                parameter));
        }
    }

    alpha_ = dynamics[0];
    beta_ = dynamics[1];
    gamma_ = dynamics[2];
}

double DynamicMarketingScenario::impact_for(rng::RandomSource &random, model::Person &person,
                                             int /*time*/, const Identifier & /*risk_factor_key*/,
                                             double value) {
    const int band = band_of(person.age);
    if (band < 0) {
        return value;
    }

    // One draw per person per year whatever branch is taken, so the two scenarios stay in step on
    // a shared seed.
    const double draw = random.next_double();

    const auto seen = book_.find(person.id());
    if (seen == book_.end()) {
        if (draw < alpha_) {
            book_.emplace(person.id(), band);
            return value + impact_value(band);
        }
        return value;
    }

    if (seen->second == kFormerlyExposed) {
        // Lapsed. Gamma is the chance of taking it up again.
        if (draw < gamma_) {
            seen->second = band;
            return value + impact_value(band);
        }
        return value;
    }

    if (draw < beta_) {
        // Lapses this year: the impact they were carrying is taken back off.
        const double difference = -impact_value(seen->second);
        seen->second = kFormerlyExposed;
        return value + difference;
    }

    const double difference = impact_value(band) - impact_value(seen->second);
    seen->second = band;
    return value + difference;
}

// --- fiscal -----------------------------------------------------------------------------------

FiscalScenario::FiscalScenario(config::InterventionSpec definition)
    : BandedInterventionScenario{std::move(definition), 3} {
    const auto &type = this->definition().impact_type;
    if (core::case_insensitive::equals(type, "pessimist")) {
        impact_type_ = ImpactType::pessimist;
    } else if (core::case_insensitive::equals(type, "optimist")) {
        impact_type_ = ImpactType::optimist;
    } else {
        throw diag::InternalError(fmt::format(
            "'fiscal' needs an impact_type of 'pessimist' or 'optimist', not '{}'", type));
    }
}

double FiscalScenario::impact_for(rng::RandomSource & /*random*/, model::Person &person,
                                   int /*time*/, const Identifier &risk_factor_key,
                                   double value) {
    const int band = band_of(person.age);
    if (band < 0) {
        return value;
    }

    // A tax changes consumption in proportion to what the person consumes, so the impact is a
    // share of their own value rather than a fixed amount.
    const auto current = person.try_risk_factor_value(risk_factor_key);
    if (!current.has_value()) {
        return value;
    }

    const auto seen = book_.find(person.id());
    if (seen == book_.end()) {
        book_.emplace(person.id(), band);
        return value + *current * impact_value(band);
    }

    if (seen->second == band) {
        return value;
    }

    // Moving up a band. The optimist reading is that whoever changed their habits as an
    // adolescent keeps them, so the adult effect never arrives; the pessimist reading is that the
    // effect decays to the adult one. This is the only difference between the two, and it only
    // ever applies on the way into the last band.
    const bool into_last_band = static_cast<std::size_t>(band) + 1 == impacts().size();
    if (into_last_band && impact_type_ == ImpactType::optimist) {
        return value;
    }

    const double difference = impact_value(band) - impact_value(seen->second);
    seen->second = band;
    return value + *current * difference;
}

// --- physical activity ------------------------------------------------------------------------

PhysicalActivityScenario::PhysicalActivityScenario(config::InterventionSpec definition)
    : BandedInterventionScenario{std::move(definition), 2} {
    const auto &rates = this->definition().coverage_rates;
    if (rates.empty()) {
        throw diag::InternalError("'physical_activity' needs a coverage rate");
    }
    coverage_rate_ = rates.front();
    if (coverage_rate_ < 0.0 || coverage_rate_ > 1.0) {
        throw diag::InternalError(fmt::format(
            "the 'physical_activity' coverage rate must be in [0, 1], not {}", coverage_rate_));
    }
}

double PhysicalActivityScenario::impact_for(rng::RandomSource &random, model::Person &person,
                                             int time, const Identifier & /*risk_factor_key*/,
                                             double value) {
    const auto &child = impacts().front();
    const bool still_a_child = !child.to_age.has_value() || person.age <= *child.to_age;

    // One draw per person per year, always, so the stream does not depend on the branch.
    const double draw = random.next_double();

    const auto seen = book_.find(person.id());
    if (seen == book_.end()) {
        // The coverage draw happens once, in childhood: a programme delivered in schools either
        // reaches somebody while they are at school or never does.
        if (!still_a_child || draw >= coverage_rate_) {
            book_.emplace(person.id(), kNoEffect);
            return value;
        }
        book_.emplace(person.id(), 0);
        return value + child.impact_value;
    }

    if (seen->second == 0 && !still_a_child) {
        // Grown out of the child band: the difference between the adult effect and the child one.
        seen->second = time;
        return value + impacts()[1].impact_value - child.impact_value;
    }

    return value;
}

// --- food labelling ---------------------------------------------------------------------------

FoodLabellingScenario::FoodLabellingScenario(config::InterventionSpec definition)
    : BandedInterventionScenario{std::move(definition), 1} {
    const auto &spec = this->definition();

    if (spec.coverage_rates.size() != 2) {
        throw diag::InternalError(
            fmt::format("'food_labelling' needs two coverage rates (short term, long term), and "
                        "has {}",
                        spec.coverage_rates.size()));
    }
    short_term_rate_ = spec.coverage_rates[0];
    long_term_rate_ = spec.coverage_rates[1];

    if (!spec.coverage_cutoff_time.has_value()) {
        throw diag::InternalError("'food_labelling' needs a coverage cutoff time");
    }
    cutoff_time_ = *spec.coverage_cutoff_time;

    if (!spec.child_cutoff_age.has_value()) {
        throw diag::InternalError("'food_labelling' needs a child cutoff age");
    }
    child_cutoff_age_ = *spec.child_cutoff_age;

    if (spec.coefficients.size() != 4) {
        throw diag::InternalError(
            fmt::format("'food_labelling' needs four transfer coefficients (child male, child "
                        "female, adult male, adult female), and has {}",
                        spec.coefficients.size()));
    }
    std::copy_n(spec.coefficients.begin(), 4, transfer_.begin());

    if (spec.adjustments.empty()) {
        throw diag::InternalError("'food_labelling' needs an adjustment risk factor");
    }
    adjustment_factor_ = Identifier{spec.adjustments.front().risk_factor};
    adjustment_value_ = spec.adjustments.front().value;
}

double FoodLabellingScenario::transfer_for(const model::Person &person) const noexcept {
    const bool child = person.age <= child_cutoff_age_;
    const bool male = person.gender == core::Gender::male;
    if (child) {
        return male ? transfer_[0] : transfer_[1];
    }
    return male ? transfer_[2] : transfer_[3];
}

double FoodLabellingScenario::impact_for(rng::RandomSource &random, model::Person &person,
                                          int time, const Identifier & /*risk_factor_key*/,
                                          double value) {
    const double draw = random.next_double();

    const auto elapsed =
        static_cast<unsigned int>(std::max(0, time - definition().active_period.start_time));

    const auto seen = book_.find(person.id());

    // A label is noticed at a higher rate while it is new. Inside that window somebody who has
    // not noticed it yet is offered it again every year; after the window closes, everyone has
    // been decided one way or the other and nobody is reconsidered.
    //
    // One difference from the baseline, recorded as B-24: when the draw succeeds the baseline
    // marks the person with `try_emplace`, which does nothing if they are already marked as
    // unaffected — so somebody who failed an early draw and passed a later one keeps their
    // "unaffected" mark and is offered the impact again, and again, every remaining year of the
    // window. Marking them affected is what the surrounding code plainly intends.
    const bool short_term = elapsed < cutoff_time_;
    if (short_term) {
        if (seen != book_.end() && seen->second != kNoEffect) {
            return value;
        }
        if (draw >= short_term_rate_) {
            book_.insert_or_assign(person.id(), kNoEffect);
            return value;
        }
    } else {
        if (seen != book_.end()) {
            return value;
        }
        if (draw >= long_term_rate_) {
            book_.emplace(person.id(), kNoEffect);
            return value;
        }
    }

    // The effect is a share of the person's own value of the adjusted risk factor, scaled by the
    // transfer coefficient for their sex and age group and by the adjustment factor.
    const auto adjusted = person.try_risk_factor_value(adjustment_factor_);
    if (!adjusted.has_value()) {
        book_.insert_or_assign(person.id(), kNoEffect);
        return value;
    }

    book_.insert_or_assign(person.id(), time);
    return value + transfer_for(person) * impacts().front().impact_value * *adjusted *
                       adjustment_value_;
}

} // namespace hgps::sim
