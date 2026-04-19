#include "default_cancer_model.h"

#include "data/person.h"
#include "hgps_input/data/pif_data.h"
#include "simulation/runtime_context.h"

#include <algorithm>
#include <fmt/color.h>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace hgps {

DefaultCancerModel::DefaultCancerModel(DiseaseDefinition &definition, WeightModel &&classifier,
                                       const core::IntegerInterval &age_range)
    : definition_{definition}, weight_classifier_{std::move(classifier)},
      average_relative_risk_{create_age_gender_table<double>(age_range)} {
    if (definition_.get().identifier().group != core::DiseaseGroup::cancer) {
        throw std::invalid_argument("Disease definition group mismatch, must be 'cancer'.");
    }
}

core::DiseaseGroup DefaultCancerModel::group() const noexcept { return core::DiseaseGroup::cancer; }

const core::Identifier &DefaultCancerModel::disease_type() const noexcept {
    return definition_.get().identifier().code;
}

void DefaultCancerModel::initialise_disease_status(RuntimeContext &context) {
    const int prevalence_id = definition_.get().table().at(MeasureKey::prevalence);
    const auto relative_risk_table = calculate_average_relative_risk(context, false);

    for (auto &person : context.population()) {
        if (!person.is_active() || !definition_.get().table().contains(person.age)) {
            continue;
        }

        double relative_risk = 1.0;
        relative_risk *= calculate_relative_risk_for_risk_factors(person);

        const double average_relative_risk = relative_risk_table(person.age, person.gender);
        const double prevalence = definition_.get().table()(person.age, person.gender).at(prevalence_id);
        const double probability =
            std::clamp(prevalence * relative_risk / average_relative_risk, 0.0, 1.0);

        if (context.random().next_double() < probability) {
            const int time_since_onset = calculate_time_since_onset(context, person.gender);
            person.diseases[disease_type()] = Disease{.status = DiseaseStatus::active,
                                                      .start_time = 0,
                                                      .time_since_onset = time_since_onset};
        }
    }
}

void DefaultCancerModel::initialise_average_relative_risk(RuntimeContext &context) {
    average_relative_risk_ = calculate_average_relative_risk(context, true);
}

void DefaultCancerModel::update_disease_status(RuntimeContext &context) {
    update_remission_cases(context);
    update_incidence_cases(context);
}

double DefaultCancerModel::get_excess_mortality(const Person &person) const {
    const int mortality_id = definition_.get().table().at(MeasureKey::mortality);

    const auto &disease_info = person.diseases.at(disease_type());
    const auto max_onset = definition_.get().parameters().max_time_since_onset;
    if (disease_info.time_since_onset < 0 || disease_info.time_since_onset >= max_onset) {
        return 0.0;
    }

    const double excess_mortality =
        definition_.get().table()(person.age, person.gender).at(mortality_id);
    const auto &sex_death_weights =
        definition_.get().parameters().death_weight.at(disease_info.time_since_onset);
    const double death_weight =
        (person.gender == core::Gender::male) ? sex_death_weights.males : sex_death_weights.females;

    return excess_mortality * death_weight;
}

DoubleAgeGenderTable DefaultCancerModel::calculate_average_relative_risk(
    RuntimeContext &context, const bool include_disease_relative_risk) {
    const auto &age_range = context.inputs().settings().age_range();
    auto sum = create_age_gender_table<double>(age_range);
    auto count = create_age_gender_table<double>(age_range);

    for (const auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }

        double relative_risk = 1.0;
        relative_risk *= calculate_relative_risk_for_risk_factors(person);
        if (include_disease_relative_risk) {
            relative_risk *= calculate_relative_risk_for_diseases(person);
        }

        sum(person.age, person.gender) += relative_risk;
        count(person.age, person.gender) += 1.0;
    }

    auto result = create_age_gender_table<double>(age_range);
    constexpr double default_average = 1.0;

    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        double male_average = default_average;
        double female_average = default_average;

        if (sum.contains(age)) {
            const double male_count = count(age, core::Gender::male);
            if (male_count > 0.0) {
                male_average = sum(age, core::Gender::male) / male_count;
            }

            const double female_count = count(age, core::Gender::female);
            if (female_count > 0.0) {
                female_average = sum(age, core::Gender::female) / female_count;
            }
        }

        result(age, core::Gender::male) = male_average;
        result(age, core::Gender::female) = female_average;
    }

    return result;
}

double DefaultCancerModel::calculate_relative_risk_for_risk_factors(const Person &person) const {
    const auto &relative_risk_tables = definition_.get().relative_risk_factors();

    double relative_risk = 1.0;
    for (const auto &[factor_name, factor_value] : person.risk_factors) {
        if (!relative_risk_tables.contains(factor_name)) {
            continue;
        }

        const auto factor_value_adjusted = static_cast<float>(
            weight_classifier_.adjust_risk_factor_value(person, factor_name, factor_value));

        const auto &rr_table = relative_risk_tables.at(factor_name);
        relative_risk *= rr_table.at(person.gender)(person.age, factor_value_adjusted);
    }

    return relative_risk;
}

double DefaultCancerModel::calculate_relative_risk_for_diseases(const Person &person) const {
    const auto &relative_risk_tables = definition_.get().relative_risk_diseases();

    double relative_risk = 1.0;
    for (const auto &[disease_name, disease_state] : person.diseases) {
        if (!relative_risk_tables.contains(disease_name)) {
            continue;
        }

        if (disease_state.status == DiseaseStatus::active) {
            const auto &rr_table = relative_risk_tables.at(disease_name);
            relative_risk *= rr_table(person.age, person.gender);
        }
    }

    return relative_risk;
}

void DefaultCancerModel::update_remission_cases(RuntimeContext &context) {
    const int max_onset = definition_.get().parameters().max_time_since_onset;

    for (auto &person : context.population()) {
        if (!person.is_active() || person.age == 0) {
            continue;
        }

        if (!person.diseases.contains(disease_type()) ||
            person.diseases.at(disease_type()).status != DiseaseStatus::active) {
            continue;
        }

        auto &disease = person.diseases.at(disease_type());
        ++disease.time_since_onset;
        if (disease.time_since_onset >= max_onset) {
            disease.status = DiseaseStatus::free;
            disease.time_since_onset = -1;
        }
    }
}

void DefaultCancerModel::update_incidence_cases(RuntimeContext &context) {
    const int incidence_id = definition_.get().table().at(MeasureKey::incidence);
    const auto &table = definition_.get().table();
    const auto disease_code = disease_type();

    const bool apply_pif =
        definition_.get().has_pif_data() && context.scenario().type() == ScenarioType::intervention;

    const int year_post_intervention = apply_pif ? (context.time_now() - context.start_time()) : 0;

    const auto *pif_table = [&]() -> const input::PIFTable * {
        if (!apply_pif) {
            return nullptr;
        }

        const auto &pif_data = definition_.get().pif_data();
        const auto &pif_config = context.inputs().population_impact_fraction();
        return pif_data.get_scenario_data(pif_config.scenario);
    }();

    if (apply_pif && pif_table) {
        static std::once_flag pif_once_flag;
        std::call_once(pif_once_flag, []() {
            fmt::print(fg(fmt::color::green),
                       "PIF Analysis: Applying Population Impact Fraction adjustments to disease "
                       "incidence calculations\n");
        });
    }

    for (auto &person : context.population()) {
        if (!person.is_active()) {
            continue;
        }

        if (person.age == 0) {
            person.diseases.clear();
            continue;
        }

        if (const auto it = person.diseases.find(disease_code);
            it != person.diseases.end() && it->second.status == DiseaseStatus::active) {
            continue;
        }

        double relative_risk = 1.0;
        relative_risk *= calculate_relative_risk_for_risk_factors(person);
        relative_risk *= calculate_relative_risk_for_diseases(person);

        const double average_relative_risk = average_relative_risk_.at(person.age, person.gender);
        double probability =
            table(person.age, person.gender).at(incidence_id) * relative_risk / average_relative_risk;

        if (apply_pif && pif_table) {
            const double pif_value =
                pif_table->get_pif_value(person.age, person.gender, year_post_intervention);
            probability *= (1.0 - pif_value);
        }

        probability = std::clamp(probability, 0.0, 1.0);

        if (context.random().next_double() < probability) {
            person.diseases[disease_code] = Disease{.status = DiseaseStatus::active,
                                                    .start_time = context.time_now(),
                                                    .time_since_onset = 0};
        }
    }
}

int DefaultCancerModel::calculate_time_since_onset(RuntimeContext &context,
                                                   const core::Gender gender) const {
    const auto &pdf = definition_.get().parameters().prevalence_distribution;
    auto values = std::vector<int>{};
    auto cumulative = std::vector<double>{};

    double sum = 0.0;
    for (const auto &item : pdf) {
        const double p = (gender == core::Gender::male) ? item.second.males : item.second.females;
        sum += p;
        values.emplace_back(item.first);
        cumulative.emplace_back(sum);
    }

    return context.random().next_empirical_discrete(values, cumulative);
}

} // namespace hgps