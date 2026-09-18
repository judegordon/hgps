#include "disease_model.h"

#include "core/parallel.h"
#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"
#include "sim/scenario.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace hgps::model {
namespace {

/// The sum and count of relative risks for one age and sex.
struct RiskAccumulator {
    std::map<std::pair<int, core::Gender>, double> sum;
    std::map<std::pair<int, core::Gender>, int> count;
};

RiskAccumulator combine_risk(RiskAccumulator left, RiskAccumulator right) {
    for (const auto &[key, value] : right.sum) {
        left.sum[key] += value;
    }
    for (const auto &[key, value] : right.count) {
        left.count[key] += value;
    }
    return left;
}

} // namespace

DiseaseModelBase::DiseaseModelBase(const DiseaseDefinition &definition, WeightModel classifier,
                                   const core::IntegerInterval &age_range)
    : definition_{definition}, classifier_{std::move(classifier)},
      average_relative_risk_{create_age_gender_table<double>(age_range)} {

    // Resolve every relative-risk table once, here, rather than per person per year. Two things are
    // resolved: the factor's index in the run-wide name table, so a person is read without a name, and
    // the per-sex table pointer, so the gender lookup is a branch rather than a map probe.
    //
    // The order is `relative_risk_factors()`'s own — it is a std::map, so factor-name order — and it
    // is load-bearing: `relative_risk_for_risk_factors` multiplies along this vector, floating-point
    // multiplication is not associative, and this is the order the per-person map iteration used to
    // give (ADR 0037).
    factor_risks_.reserve(definition_.relative_risk_factors().size());
    for (const auto &[factor, by_gender] : definition_.relative_risk_factors()) {
        FactorRisk entry;
        entry.index = factor_index().intern(factor);
        entry.name = factor;
        if (const auto found = by_gender.find(core::Gender::male); found != by_gender.end()) {
            entry.male = &found->second;
        }
        if (const auto found = by_gender.find(core::Gender::female); found != by_gender.end()) {
            entry.female = &found->second;
        }
        factor_risks_.push_back(entry);
    }
}

double DiseaseModelBase::relative_risk_for_risk_factors(const Person &person) const {
    // factor_risks_ is built from a std::map, so this product is taken in factor-name order — the
    // same order, over the same set, as the loop this replaced (ADR 0037).
    double relative_risk = 1.0;
    for (const auto &factor : factor_risks_) {
        const auto *value = person.risk_factors.find_index(factor.index);
        if (value == nullptr) {
            continue;
        }

        const auto *table = factor.for_gender(person.gender);
        if (table == nullptr) {
            continue;
        }

        // A child's BMI is read as the midpoint of their weight category, because the relative
        // risks are tabulated against adult-style categories.
        const auto adjusted = static_cast<float>(
            classifier_.adjust_risk_factor_value(person, factor.name, *value));
        // The lookup tables are float, as the data is; the product is double. Stated rather
        // than implicit, because an unnoticed promotion is how precision arguments start.
        relative_risk *= static_cast<double>((*table)(static_cast<int>(person.age), adjusted));
    }

    return relative_risk;
}

double DiseaseModelBase::relative_risk_for_diseases(const Person &person) const {
    const auto &tables = definition_.relative_risk_diseases();

    double relative_risk = 1.0;
    for (const auto &[disease, state] : person.diseases) {
        if (state.status != DiseaseStatus::active) {
            continue;
        }

        const auto table = tables.find(disease);
        if (table == tables.end()) {
            continue;
        }

        relative_risk *=
            static_cast<double>(table->second(static_cast<int>(person.age), person.gender));
    }

    return relative_risk;
}

DoubleAgeGenderTable DiseaseModelBase::compute_average_relative_risk(RuntimeContext &context,
                                                                     bool include_diseases) const {
    const auto &age_range = context.age_range();
    const auto &population = context.population();

    // A pure function of person state, drawing nothing, reduced in a fixed block order — which is
    // what makes it both eligible for a parallel region (D3) and independent of the thread count
    // (D5).
    const auto totals = core::parallel::reduce_ordered(
        population.size(), RiskAccumulator{},
        [this, &population, include_diseases](std::size_t index) {
            RiskAccumulator single;
            const auto &person = population[index];
            if (!person.is_active()) {
                return single;
            }

            double relative_risk = relative_risk_for_risk_factors(person);
            if (include_diseases) {
                relative_risk *= relative_risk_for_diseases(person);
            }

            const auto key = std::make_pair(static_cast<int>(person.age), person.gender);
            single.sum[key] = relative_risk;
            single.count[key] = 1;
            return single;
        },
        combine_risk);

    auto result = create_age_gender_table<double>(age_range);
    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            // Nobody of this age and sex alive means an average of 1: no relative risk to
            // normalise against, so the measured prevalence or incidence applies as given.
            double average = 1.0;
            const auto key = std::make_pair(age, gender);
            const auto count = totals.count.find(key);
            if (count != totals.count.end() && count->second > 0) {
                average = totals.sum.at(key) / static_cast<double>(count->second);
            }
            result.at(age, gender) = average;
        }
    }

    return result;
}

void DiseaseModelBase::initialise_average_relative_risk(RuntimeContext &context) {
    average_relative_risk_ = compute_average_relative_risk(context, true);
}

void DiseaseModelBase::update_disease_status(RuntimeContext &context) {
    // Order is load-bearing: remission frees this year's recoveries before incidence considers
    // who is at risk, so a person cannot recover and relapse in the same year.
    update_remission_cases(context);
    update_incidence_cases(context);
}

void DiseaseModelBase::update_incidence_cases(RuntimeContext &context) {
    const auto incidence_id = definition_.table().at(MeasureKey::incidence);
    const auto &table = definition_.table();
    const auto disease_code = disease_type();

    // Serial, in slot order, because it draws (determinism clause D3).
    for (auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }

        // A newborn starts free of everything: their slot may have held someone who was not.
        if (person.age == 0) {
            person.diseases.clear();
            continue;
        }

        if (const auto existing = person.diseases.find(disease_code);
            existing != person.diseases.end() &&
            existing->second.status == DiseaseStatus::active) {
            continue;
        }

        if (!table.contains(static_cast<int>(person.age))) {
            continue;
        }

        const double relative_risk =
            relative_risk_for_risk_factors(person) * relative_risk_for_diseases(person);
        const double average =
            average_relative_risk_.at(static_cast<int>(person.age), person.gender);
        const double incidence =
            table(static_cast<int>(person.age), person.gender).at(incidence_id);

        const double probability = incidence * relative_risk / average;
        if (context.random().next_double() < probability) {
            person.diseases[disease_code] = make_incident_case(context);
        }
    }
}

DefaultDiseaseModel::DefaultDiseaseModel(const DiseaseDefinition &definition,
                                         WeightModel classifier,
                                         const core::IntegerInterval &age_range)
    : DiseaseModelBase{definition, std::move(classifier), age_range} {
    if (definition.identifier().group == core::DiseaseGroup::cancer) {
        throw std::invalid_argument("Disease definition group mismatch, must not be 'cancer'.");
    }
}

void DefaultDiseaseModel::initialise_disease_status(RuntimeContext &context) {
    const auto prevalence_id = definition().table().at(MeasureKey::prevalence);

    // The average here excludes other diseases, because no disease status exists yet.
    const auto averages = compute_average_relative_risk(context, false);

    for (auto &person : context.population()) {
        if (!person.is_active() || !definition().table().contains(static_cast<int>(person.age))) {
            continue;
        }

        const double relative_risk = relative_risk_for_risk_factors(person);
        const double average = averages.at(static_cast<int>(person.age), person.gender);
        const double prevalence =
            definition().table()(static_cast<int>(person.age), person.gender).at(prevalence_id);

        const double probability = prevalence * relative_risk / average;
        if (context.random().next_double() < probability) {
            // start_time 0 means the disease predates the simulation.
            person.diseases[disease_type()] =
                Disease{.status = DiseaseStatus::active, .start_time = 0};
        }
    }
}

double DefaultDiseaseModel::get_excess_mortality(const Person &person) const {
    const auto mortality_id = definition().table().at(MeasureKey::mortality);

    if (definition().table().contains(static_cast<int>(person.age))) {
        return definition().table()(static_cast<int>(person.age), person.gender).at(mortality_id);
    }

    return 0.0;
}

void DefaultDiseaseModel::update_remission_cases(RuntimeContext &context) {
    const auto remission_id = definition().table().at(MeasureKey::remission);

    for (auto &person : context.population()) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        const auto state = person.diseases.find(disease_type());
        if (state == person.diseases.end() || state->second.status != DiseaseStatus::active) {
            continue;
        }

        if (!definition().table().contains(static_cast<int>(person.age))) {
            continue;
        }

        const double probability =
            definition().table()(static_cast<int>(person.age), person.gender).at(remission_id);
        if (context.random().next_double() < probability) {
            state->second.status = DiseaseStatus::free;
        }
    }
}

Disease DefaultDiseaseModel::make_incident_case(RuntimeContext &context) {
    return Disease{.status = DiseaseStatus::active, .start_time = context.time_now()};
}

DefaultCancerModel::DefaultCancerModel(const DiseaseDefinition &definition,
                                       WeightModel classifier,
                                       const core::IntegerInterval &age_range)
    : DiseaseModelBase{definition, std::move(classifier), age_range} {
    if (definition.identifier().group != core::DiseaseGroup::cancer) {
        throw std::invalid_argument("Disease definition group mismatch, must be 'cancer'.");
    }
}

void DefaultCancerModel::initialise_disease_status(RuntimeContext &context) {
    const auto prevalence_id = definition().table().at(MeasureKey::prevalence);
    const auto averages = compute_average_relative_risk(context, false);

    for (auto &person : context.population()) {
        if (!person.is_active() || !definition().table().contains(static_cast<int>(person.age))) {
            continue;
        }

        const double relative_risk = relative_risk_for_risk_factors(person);
        const double average = averages.at(static_cast<int>(person.age), person.gender);
        const double prevalence =
            definition().table()(static_cast<int>(person.age), person.gender).at(prevalence_id);

        const double probability = prevalence * relative_risk / average;
        if (context.random().next_double() < probability) {
            // The draw order matters: the hazard comes first, then the onset time, only for the
            // people who turn out to have the disease.
            const int time_since_onset = calculate_time_since_onset(context, person.gender);
            person.diseases[disease_type()] = Disease{.status = DiseaseStatus::active,
                                                      .start_time = 0,
                                                      .time_since_onset = time_since_onset};
        }
    }
}

double DefaultCancerModel::get_excess_mortality(const Person &person) const {
    const auto state = person.diseases.find(disease_type());
    if (state == person.diseases.end()) {
        return 0.0;
    }

    const auto max_onset = definition().parameters().max_time_since_onset;
    if (state->second.time_since_onset < 0 || state->second.time_since_onset >= max_onset) {
        return 0.0;
    }

    if (!definition().table().contains(static_cast<int>(person.age))) {
        return 0.0;
    }

    const auto mortality_id = definition().table().at(MeasureKey::mortality);
    const double excess =
        definition().table()(static_cast<int>(person.age), person.gender).at(mortality_id);

    const auto weights = definition().parameters().death_weight.find(state->second.time_since_onset);
    if (weights == definition().parameters().death_weight.end()) {
        throw diag::InternalError(
            fmt::format("{} has no death weight for time since onset {}",
                        disease_type().to_string(), state->second.time_since_onset));
    }

    return excess * weights->second.at(person.gender);
}

void DefaultCancerModel::update_remission_cases(RuntimeContext &context) {
    // Cancer has no remission draw: a case runs for as long as the prevalence distribution
    // covers, then ends. No RNG here at all.
    const auto max_onset = definition().parameters().max_time_since_onset;

    for (auto &person : context.population()) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        const auto state = person.diseases.find(disease_type());
        if (state == person.diseases.end() || state->second.status != DiseaseStatus::active) {
            continue;
        }

        state->second.time_since_onset++;
        if (state->second.time_since_onset >= max_onset) {
            state->second.status = DiseaseStatus::free;
            state->second.time_since_onset = -1;
        }
    }
}

Disease DefaultCancerModel::make_incident_case(RuntimeContext &context) {
    return Disease{
        .status = DiseaseStatus::active, .start_time = context.time_now(), .time_since_onset = 0};
}

int DefaultCancerModel::calculate_time_since_onset(RuntimeContext &context,
                                                   core::Gender gender) const {
    const auto &distribution = definition().parameters().prevalence_distribution;
    if (distribution.empty()) {
        throw diag::InternalError(fmt::format("{} has no prevalence distribution, so the time "
                                              "since onset of a prevalent case is unknown",
                                              disease_type().to_string()));
    }

    // The map is keyed by time since onset, so both vectors are in ascending order of it.
    std::vector<int> values;
    std::vector<double> cumulative;
    values.reserve(distribution.size());
    cumulative.reserve(distribution.size());

    double sum = 0.0;
    for (const auto &[onset, by_gender] : distribution) {
        sum += by_gender.at(gender);
        values.push_back(onset);
        cumulative.push_back(sum);
    }

    return context.random().next_empirical_discrete(values, cumulative);
}

DiseaseModule::DiseaseModule(std::map<core::Identifier, std::unique_ptr<DiseaseModel>> models)
    : models_{std::move(models)} {}

void DiseaseModule::initialise_population(RuntimeContext &context) {
    // Three passes, in this order: status from prevalence, then the averages now that status
    // exists, then one year of incidence so that the cohort's disease durations are not all zero.
    for (auto &[code, model] : models_) {
        model->initialise_disease_status(context);
    }
    for (auto &[code, model] : models_) {
        model->initialise_average_relative_risk(context);
    }
    for (auto &[code, model] : models_) {
        model->update_disease_status(context);
    }
}

void DiseaseModule::update_population(RuntimeContext &context) {
    for (auto &[code, model] : models_) {
        model->update_disease_status(context);
    }
}

double DiseaseModule::excess_mortality(const core::Identifier &disease,
                                       const Person &person) const {
    const auto model = models_.find(disease);
    if (model == models_.end()) {
        // A disease a person carries that this run does not model contributes nothing. That
        // happens when a person's disease map outlives a configuration change, and it is not an
        // error.
        return 0.0;
    }

    return model->second->get_excess_mortality(person);
}

std::unique_ptr<DiseaseModel> create_disease_model(const DiseaseDefinition &definition,
                                                   WeightModel classifier,
                                                   const core::IntegerInterval &age_range) {
    switch (definition.identifier().group) {
    case core::DiseaseGroup::cancer:
        return std::make_unique<DefaultCancerModel>(definition, std::move(classifier), age_range);
    case core::DiseaseGroup::other:
        return std::make_unique<DefaultDiseaseModel>(definition, std::move(classifier), age_range);
    }

    throw diag::InternalError(fmt::format("disease '{}' has an unknown group",
                                          definition.identifier().code.to_string()));
}

} // namespace hgps::model
