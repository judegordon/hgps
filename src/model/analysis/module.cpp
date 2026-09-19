// The module's lifecycle and the year's aggregate statistics.
#include "analysis_module.h"

#include "core/parallel.h"
#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hgps::model {

AnalysisModule::AnalysisModule(AnalysisDefinition definition, WeightModel classifier,
                               core::IntegerInterval age_range, unsigned int comorbidities)
    : definition_{std::move(definition)}, classifier_{std::move(classifier)},
      residual_disability_weight_{create_age_gender_table<double>(age_range)},
      comorbidities_{comorbidities} {}

void AnalysisModule::initialise_population(RuntimeContext &context) {
    const auto &age_range = context.age_range();
    const auto &population = context.population();

    // The mean modelled disability weight per age and sex, as a fixed-order reduction. The
    // baseline accumulates it into a shared table under a mutex, inside a parallel loop, having
    // first built a separate is_active vector in another parallel loop (audit N-7).
    struct Accumulator {
        std::map<std::pair<int, core::Gender>, double> sum;
        std::map<std::pair<int, core::Gender>, int> count;
    };

    const auto totals = core::parallel::reduce_ordered(
        population.size(), Accumulator{},
        [this, &population](std::size_t index) {
            Accumulator single;
            const auto &person = population[index];
            if (!person.is_active()) {
                return single;
            }

            double healthy = 1.0;
            for (const auto &[disease, state] : person.diseases) {
                if (state.status != DiseaseStatus::active) {
                    continue;
                }
                const auto weight = definition_.disability_weights().find(disease);
                if (weight != definition_.disability_weights().end()) {
                    healthy *= 1.0 - static_cast<double>(weight->second);
                }
            }

            const auto key = std::make_pair(static_cast<int>(person.age), person.gender);
            single.sum[key] = healthy;
            single.count[key] = 1;
            return single;
        },
        [](Accumulator left, Accumulator right) {
            for (const auto &[key, value] : right.sum) {
                left.sum[key] += value;
            }
            for (const auto &[key, value] : right.count) {
                left.count[key] += value;
            }
            return left;
        });

    auto expected_sum = create_age_gender_table<double>(age_range);
    auto expected_count = create_age_gender_table<int>(age_range);
    for (const auto &[key, value] : totals.sum) {
        const auto [age, gender] = key;
        if (expected_sum.contains(age, gender)) {
            expected_sum.at(age, gender) = value;
            expected_count.at(age, gender) = totals.count.at(key);
        }
    }

    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            residual_disability_weight_.at(age, gender) =
                calculate_residual_disability_weight(age, gender, expected_sum, expected_count);
        }
    }

    initialise_output_channels(context);
}

void AnalysisModule::update_population(RuntimeContext &context) {
    // Nothing to update: a year's analysis is produced by analyse(), which the engine calls and
    // whose result it hands to the writer. The baseline publishes from here onto a concurrent
    // queue, which is what makes its output row order depend on thread scheduling (B-01).
    if (channels_.empty()) {
        initialise_output_channels(context);
    }
}

ModelResult AnalysisModule::analyse(RuntimeContext &context) const {
    const auto age_count = static_cast<std::size_t>(context.age_range().upper()) + 1;

    ModelResult result{age_count};
    calculate_historical_statistics(context, result);
    calculate_population_statistics(context, result.series);

    return result;
}

namespace {

/// @brief The interval outside which a value is not a description of a person at all.
///
/// **Not the configured modelling range.** That is narrower, it is the model's own business, and
/// `KevinHallModel::validate_weight` already treats a body above it as implausible-but-
/// describable and counts it rather than refusing the run. These are the bounds at which a
/// number stops being a measurement of anything, set far outside any cohort anybody would
/// simulate so that crossing one is evidence of a defect rather than of an unusual draw: the
/// heaviest human reliably recorded weighed about 635 kg and the tallest stood 272 cm.
struct Describable {
    double lower{-std::numeric_limits<double>::infinity()};
    double upper{std::numeric_limits<double>::infinity()};

    bool contains(double value) const noexcept {
        // Finiteness first, and it is the whole check for every factor that has no physical
        // bound: the two comparisons against an infinite bound are true for any finite value.
        return std::isfinite(value) && value >= lower && value <= upper;
    }
};

/// @brief The bounds for the factors that have a physical meaning this code can state.
///
/// Everything else is checked for finiteness only. A nutrient intake has no ceiling anybody
/// here can defend, and inventing one would make the guard a modelling opinion.
Describable describable_range(const core::Identifier &factor) {
    static const std::map<core::Identifier, Describable> bounds{
        // A gram, and a tonne. Below the first is not a body and above the second is not one
        // either; the configured range in every shipped example is far inside both.
        {core::Identifier{"weight"}, Describable{0.001, 1000.0}},
        {core::Identifier{"height"}, Describable{1.0, 300.0}},
        {core::Identifier{"bmi"}, Describable{0.001, 1000.0}},
        // Zero energy is possible for a day and negative energy is not. The ceiling is about
        // 240,000 kcal, which is fifty times what anybody eats.
        {core::Identifier{"energyintake"}, Describable{0.0, 1.0e6}},
    };
    const auto found = bounds.find(factor);
    return found == bounds.end() ? Describable{} : found->second;
}

/// @brief "and between 0.001 and 1000" — the half of the message that depends on the factor.
std::string bound_sentence(const Describable &bounds) {
    if (!std::isfinite(bounds.lower) && !std::isfinite(bounds.upper)) {
        return "";
    }
    return fmt::format(" and between {:g} and {:g}", bounds.lower, bounds.upper);
}

} // namespace

void AnalysisModule::calculate_historical_statistics(RuntimeContext &context,
                                                     ModelResult &result) const {
    // Both sexes are present in every accumulator from the start, and the accumulators are
    // GenderValue rather than a map keyed by sex. A map filled in as people are walked has an
    // entry only for the sexes that actually occurred, so reading it back for the other one
    // throws — which is a crash that appears only when a disease, or a whole sex, happens to be
    // absent from the cohort. Pinned by
    // `TestSimulation.ReportsAStatisticForBothSexesEvenWhenOneHasNobody`.

    // Every level-1-and-above factor is reported as a mean; level 0 factors are the demographic
    // ones, reported from their own members.
    std::map<core::Identifier, GenderValue<double>> risk_factors;
    for (const auto &entry : context.mapping()) {
        if (entry.level() > 0) {
            risk_factors.emplace(entry.key(), GenderValue<double>{});
        }
    }

    // The same accumulators, flat, each carrying the bounds its value has to be inside. Flat
    // because this is the one loop in the program that runs per person per factor per year, and
    // the bounds have to be beside the value for the check to cost nothing: a `std::isfinite`
    // and two comparisons on a double already in a register, against a map probe per factor per
    // person if the bounds were looked up here (ADR 0050).
    struct Checked {
        core::Identifier factor;
        GenderValue<double> *sum;
        Describable bounds;
    };
    std::vector<Checked> checked;
    checked.reserve(risk_factors.size());
    for (auto &[factor, by_gender] : risk_factors) {
        checked.push_back(Checked{factor, &by_gender, describable_range(factor)});
    }

    std::map<core::Identifier, GenderValue<int>> prevalence;
    for (const auto &disease : context.diseases()) {
        prevalence.emplace(disease.code, GenderValue<int>{});
    }

    std::map<unsigned int, ResultByGender> comorbidity;
    for (unsigned int i = 0; i <= comorbidities_; ++i) {
        comorbidity.emplace(i, ResultByGender{});
    }

    GenderValue<int> age_sum{};
    GenderValue<int> counts{};

    const auto analysis_time = static_cast<unsigned int>(context.time_now());
    const auto max_age = static_cast<unsigned int>(context.age_range().upper());

    int dead = 0;
    int migrated = 0;

    const auto &population = context.population();
    for (const auto &person : population) {
        if (!person.is_active()) {
            // A person who left this year is still counted, once, in the year they left.
            if (person.has_emigrated() && person.time_of_migration() == analysis_time) {
                ++migrated;
            }
            if (!person.is_alive() && person.time_of_death() == analysis_time) {
                ++dead;
            }
            continue;
        }

        age_sum.at(person.gender) += static_cast<int>(person.age);
        counts.at(person.gender) += 1;

        for (const auto &entry : checked) {
            const auto value = person.risk_factors.find(entry.factor);
            if (value == person.risk_factors.end()) {
                // A factor this project's models do not assign contributes nothing, which is
                // what it has always done and is not the same as an impossible value.
                continue;
            }

            const double factor_value = value->second;
            if (!entry.bounds.contains(factor_value)) [[unlikely]] {
                // The last gate before a number becomes output. Everything upstream of here is
                // a model with its own guards; this is the one place that knows a value is
                // about to be written and can still say who it belongs to (ADR 0050).
                //
                // The baseline replaces a NaN with zero here and says nothing
                // (`analysis_module.cpp:388`), which turns an impossible value into a quietly
                // wrong mean — and does not look at an infinity at all. B-30 puts that back.
                if (substitutes_impossible_values_) {
                    entry.sum->at(person.gender) +=
                        std::isnan(factor_value) ? 0.0 : factor_value;
                    continue;
                }
                throw diag::InternalError(fmt::format(
                    "person {} ({}, age {}) has {} = {} in {}, which no result file can carry. "
                    "A value has to be finite{}; this one is not, so the run stops here rather "
                    "than writing a mean that includes it",
                    person.id(), person.gender == core::Gender::male ? "male" : "female",
                    person.age, context.mapping().at(entry.factor).name(), factor_value,
                    context.time_now(), bound_sentence(entry.bounds)));
            }

            entry.sum->at(person.gender) += factor_value;
        }

        unsigned int active_diseases = 0;
        for (const auto &[disease, state] : person.diseases) {
            if (state.status == DiseaseStatus::active) {
                ++active_diseases;
                const auto counted = prevalence.find(disease);
                if (counted != prevalence.end()) {
                    counted->second.at(person.gender) += 1;
                }
            }
        }

        comorbidity.at(std::min(active_diseases, comorbidities_)).at(person.gender) += 1;
    }

    // Guarded against zero, so an empty sex reports zeros rather than NaNs.
    const auto males = std::max(1, counts.male);
    const auto females = std::max(1, counts.female);

    result.population_size = static_cast<int>(population.size());
    result.number_alive = GenderValue<int>{males, females};
    result.number_dead = dead;
    result.number_emigrated = migrated;
    result.average_age.male = age_sum.male * 1.0 / males;
    result.average_age.female = age_sum.female * 1.0 / females;

    for (const auto &[factor, by_gender] : risk_factors) {
        const auto &name = context.mapping().at(factor).name();
        result.risk_factor_average.emplace(
            name, ResultByGender{.male = by_gender.male / males,
                                 .female = by_gender.female / females});
    }

    for (const auto &disease : context.diseases()) {
        result.disease_prevalence.emplace(
            disease.code.to_string(),
            ResultByGender{.male = prevalence.at(disease.code).male * 100.0 / males,
                           .female = prevalence.at(disease.code).female * 100.0 / females});
    }

    for (const auto &[metric, value] : context.metrics()) {
        result.metrics.emplace(metric, value);
    }

    for (const auto &[number, by_gender] : comorbidity) {
        result.comorbidity.emplace(number,
                                   ResultByGender{.male = by_gender.male * 100.0 / males,
                                                  .female = by_gender.female * 100.0 / females});
    }

    result.indicators = calculate_dalys(population, max_age, analysis_time);

    if (income_analysis_) {
        calculate_income_based_statistics(context, result);
    }
}

} // namespace hgps::model
