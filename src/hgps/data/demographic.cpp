#include "demographic.h"

#include "converter.h"
#include "events/sync_message.h"
#include "hgps_core/utils/thread_util.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <oneapi/tbb/parallel_for_each.h>

namespace {

using ResidualMortalityMessage = hgps::SyncDataMessage<hgps::GenderTable<int, double>>;

double safe_rate(double numerator, double denominator) noexcept {
    if (denominator <= 0.0) {
        return 0.0;
    }
    return numerator / denominator;
}

double clamp_probability(double value) noexcept { return std::clamp(value, 0.0, 1.0); }

} // namespace

namespace hgps {

DemographicModule::DemographicModule(std::map<int, std::map<int, PopulationRecord>> &&pop_data,
                                     LifeTable &&life_table)
    : pop_data_{std::move(pop_data)}, life_table_{std::move(life_table)} {
    if (pop_data_.empty()) {
        if (!life_table_.empty()) {
            throw std::invalid_argument("empty population and life table content mismatch.");
        }

        return;
    }

    if (life_table_.empty()) {
        throw std::invalid_argument("population and empty life table content mismatch.");
    }

    const auto first_entry = pop_data_.begin();
    const auto time_range = core::IntegerInterval(first_entry->first, pop_data_.rbegin()->first);

    core::IntegerInterval age_range{};
    if (!first_entry->second.empty()) {
        age_range = core::IntegerInterval(first_entry->second.begin()->first,
                                          first_entry->second.rbegin()->first);
    }

    if (time_range != life_table_.time_limits()) {
        throw std::invalid_argument("Population and life table time limits mismatch.");
    }

    if (age_range != life_table_.age_limits()) {
        throw std::invalid_argument("Population and life table age limits mismatch.");
    }

    initialise_birth_rates();
}

SimulationModuleType DemographicModule::type() const noexcept {
    return SimulationModuleType::Demographic;
}

const std::string &DemographicModule::name() const noexcept { return name_; }

std::size_t DemographicModule::get_total_population_size(int time_year) const noexcept {
    double total = 0.0;
    if (pop_data_.contains(time_year)) {
        const auto &year_data = pop_data_.at(time_year);
        total = std::accumulate(year_data.begin(), year_data.end(), 0.0,
                                [](double previous, const auto &element) {
                                    return previous + element.second.total();
                                });
    }

    return static_cast<std::size_t>(total);
}

double DemographicModule::get_total_deaths(int time_year) const noexcept {
    if (life_table_.contains_time(time_year)) {
        return life_table_.get_total_deaths_at(time_year);
    }

    return 0.0;
}

const std::map<int, PopulationRecord> &
DemographicModule::get_population_distribution(int time_year) const {
    return pop_data_.at(time_year);
}

std::map<int, DoubleGenderValue>
DemographicModule::get_age_gender_distribution(int time_year) const noexcept {
    std::map<int, DoubleGenderValue> result;
    if (!pop_data_.contains(time_year)) {
        return result;
    }

    const auto &year_data = pop_data_.at(time_year);
    if (!year_data.empty()) {
        const auto total_population = get_total_population_size(time_year);
        if (total_population == 0) {
            return result;
        }

        const double total_ratio = 1.0 / static_cast<double>(total_population);
        for (const auto &age : year_data) {
            result.emplace(age.first, DoubleGenderValue(age.second.males * total_ratio,
                                                        age.second.females * total_ratio));
        }
    }

    return result;
}

DoubleGenderValue DemographicModule::get_birth_rate(int time_year) const noexcept {
    if (birth_rates_.contains(time_year)) {
        return DoubleGenderValue{birth_rates_(time_year, core::Gender::male),
                                 birth_rates_(time_year, core::Gender::female)};
    }

    return DoubleGenderValue{0.0, 0.0};
}

double DemographicModule::get_residual_death_rate(int age, core::Gender gender) const noexcept {
    if (residual_death_rates_.contains(age)) {
        return residual_death_rates_.at(age, gender);
    }

    return 0.0;
}

void DemographicModule::initialise_population(RuntimeContext &context) {
    const auto age_gender_dist = get_age_gender_distribution(context.start_time());
    auto index = 0;
    const auto pop_size = static_cast<int>(context.population().size());
    const auto entry_total = static_cast<int>(age_gender_dist.size());

    for (auto entry_count = 1; auto &entry : age_gender_dist) {
        auto num_males = static_cast<int>(std::round(pop_size * entry.second.males));
        auto num_females = static_cast<int>(std::round(pop_size * entry.second.females));
        const auto num_required = index + num_males + num_females;
        auto pop_diff = pop_size - num_required;

        if (entry_count == entry_total && pop_diff > 0) {
            const auto half_diff = pop_diff / 2;
            num_males += half_diff;
            num_females += half_diff;
            if (entry.second.males > entry.second.females) {
                num_males += pop_diff - (half_diff * 2);
            } else {
                num_females += pop_diff - (half_diff * 2);
            }

            [[maybe_unused]] const auto pop_diff_new = pop_size - (index + num_males + num_females);
            assert(pop_diff_new == 0);
        } else if (pop_diff < 0) {
            pop_diff *= -1;
            if (entry.second.males > entry.second.females) {
                num_females -= pop_diff;
                if (num_females < 0) {
                    num_males += num_females;
                }
            } else {
                num_males -= pop_diff;
                if (num_males < 0) {
                    num_females += num_males;
                }
            }

            num_males = std::max(0, num_males);
            num_females = std::max(0, num_females);

            [[maybe_unused]] const auto pop_diff_new = pop_size - (index + num_males + num_females);
            assert(pop_diff_new == 0);
        }

        for (auto i = 0; i < num_males; i++) {
            context.population()[index + i].age = entry.first;
            context.population()[index + i].gender = core::Gender::male;
            initialise_region(context, context.population()[index + i], context.random());
            initialise_ethnicity(context, context.population()[index + i], context.random());
        }
        index += num_males;

        for (auto i = 0; i < num_females; i++) {
            context.population()[index + i].age = entry.first;
            context.population()[index + i].gender = core::Gender::female;
            initialise_region(context, context.population()[index + i], context.random());
            initialise_ethnicity(context, context.population()[index + i], context.random());
        }
        index += num_females;

        entry_count++;
    }

    assert(index == pop_size);
}

void DemographicModule::update_population(RuntimeContext &context,
                                          const DiseaseModule &disease_host) {
    auto residual_future = core::run_async(&DemographicModule::update_residual_mortality, this,
                                           std::ref(context), std::ref(disease_host));

    const auto initial_pop_size = context.population().current_active_size();
    const auto expected_pop_size = get_total_population_size(context.time_now());
    const auto expected_num_deaths = get_total_deaths(context.time_now());

    residual_future.get();
    const auto number_of_deaths = update_age_and_death_events(context, disease_host);

    const auto last_year_births_rate = get_birth_rate(context.time_now() - 1);
    const auto number_of_boys = static_cast<int>(last_year_births_rate.males * initial_pop_size);
    const auto number_of_girls = static_cast<int>(last_year_births_rate.females * initial_pop_size);

    context.population().add_newborn_babies(number_of_boys, core::Gender::male, context.time_now());
    context.population().add_newborn_babies(number_of_girls, core::Gender::female,
                                            context.time_now());

    int newborn_count = 0;
    for (auto &person : context.population()) {
        if (person.is_active() && person.age == 0) {
            newborn_count++;
            initialise_region(context, person, context.random());
            initialise_ethnicity(context, person, context.random());

            const auto &demographics = context.inputs().project_requirements().demographics;
            if (demographics.region && (person.region == "unknown" || person.region.empty())) {
                throw core::InternalError(fmt::format(
                    "Newborn #{} region was not initialized. Region data may not be available for "
                    "age 0, or region_prevalence_ is empty.",
                    newborn_count));
            }
            if (demographics.ethnicity &&
                (person.ethnicity == "unknown" || person.ethnicity.empty())) {
                throw core::InternalError(fmt::format(
                    "Newborn #{} ethnicity was not initialized. Ethnicity data may not be "
                    "available for the required age group, or ethnicity_prevalence_ is empty.",
                    newborn_count));
            }
        }
    }

    const auto simulated_death_rate =
        initial_pop_size > 0 ? (number_of_deaths * 1000.0 / initial_pop_size) : 0.0;
    const auto expected_death_rate =
        expected_pop_size > 0 ? (expected_num_deaths * 1000.0 / expected_pop_size) : 0.0;
    const auto percent_difference =
        expected_death_rate > 0.0 ? 100.0 * (simulated_death_rate / expected_death_rate - 1.0) : 0.0;

    context.metrics()["SimulatedDeathRate"] = simulated_death_rate;
    context.metrics()["ExpectedDeathRate"] = expected_death_rate;
    context.metrics()["DeathRateDeltaPercent"] = percent_difference;
}

void DemographicModule::update_residual_mortality(RuntimeContext &context,
                                                  const DiseaseModule &disease_host) {
    if (context.scenario().type() == ScenarioType::baseline) {
        auto residual_mortality = calculate_residual_mortality(context, disease_host);
        residual_death_rates_ = residual_mortality;

        context.sync_channel().send(std::make_unique<ResidualMortalityMessage>(
            context.current_run(), context.time_now(), std::move(residual_mortality)));
    } else {
        auto message = context.sync_channel().try_receive(context.sync_timeout_millis());
        if (!message.has_value()) {
            throw std::runtime_error(
                "Simulation out of sync, receive residual mortality message has timed out");
        }

        auto &base_ptr = message.value();
        auto *message_ptr = dynamic_cast<ResidualMortalityMessage *>(base_ptr.get());
        if (message_ptr == nullptr) {
            throw std::runtime_error(
                "Simulation out of sync, failed to receive a residual mortality message");
        }

        residual_death_rates_ = message_ptr->data();
    }
}

void DemographicModule::initialise_birth_rates() {
    birth_rates_ = create_integer_gender_table<double>(life_table_.time_limits());

    const auto start_time = life_table_.time_limits().lower();
    const auto end_time = life_table_.time_limits().upper();

    for (int year = start_time; year <= end_time; year++) {
        const auto &births = life_table_.get_births_at(year);
        const auto population_size = static_cast<double>(get_total_population_size(year));

        const double male_birth_rate =
            safe_rate(births.number * births.sex_ratio, (1.0 + births.sex_ratio) * population_size);
        const double female_birth_rate =
            safe_rate(births.number, (1.0 + births.sex_ratio) * population_size);

        birth_rates_.at(year, core::Gender::male) = male_birth_rate;
        birth_rates_.at(year, core::Gender::female) = female_birth_rate;
    }
}

GenderTable<int, double> DemographicModule::create_death_rates_table(int time_year) {
    auto &population = pop_data_.at(time_year);
    const auto &mortality = life_table_.get_mortalities_at(time_year);
    auto death_rates = create_integer_gender_table<double>(life_table_.age_limits());

    const auto start_age = life_table_.age_limits().lower();
    const auto end_age = life_table_.age_limits().upper();

    for (int age = start_age; age <= end_age; age++) {
        const auto male_population = static_cast<double>(population.at(age).males);
        const auto female_population = static_cast<double>(population.at(age).females);

        const auto male_rate = clamp_probability(safe_rate(mortality.at(age).males, male_population));
        const auto female_rate =
            clamp_probability(safe_rate(mortality.at(age).females, female_population));

        death_rates.at(age, core::Gender::male) = male_rate;
        death_rates.at(age, core::Gender::female) = female_rate;
    }

    return death_rates;
}

GenderTable<int, double>
DemographicModule::calculate_residual_mortality(RuntimeContext &context,
                                                const DiseaseModule &disease_host) {
    auto excess_mortality_product = create_integer_gender_table<double>(life_table_.age_limits());
    auto excess_mortality_count = create_integer_gender_table<int>(life_table_.age_limits());

    auto &pop = context.population();
    auto sum_mutex = std::mutex{};

    tbb::parallel_for_each(pop.cbegin(), pop.cend(), [&](const auto &entity) {
        if (!entity.is_active()) {
            return;
        }

        const auto product = calculate_excess_mortality_product(entity, disease_host);
        auto lock = std::unique_lock{sum_mutex};
        excess_mortality_product.at(entity.age, entity.gender) += product;
        excess_mortality_count.at(entity.age, entity.gender)++;
    });

    const auto death_rates = create_death_rates_table(context.time_now());
    auto residual_mortality = create_integer_gender_table<double>(life_table_.age_limits());

    const auto start_age = life_table_.age_limits().lower();
    const auto end_age = life_table_.age_limits().upper();
    constexpr double default_average = 1.0;

    for (int age = start_age; age <= end_age; age++) {
        auto male_average_product = default_average;
        auto female_average_product = default_average;

        const auto male_count = excess_mortality_count.at(age, core::Gender::male);
        const auto female_count = excess_mortality_count.at(age, core::Gender::female);

        if (male_count > 0) {
            male_average_product =
                excess_mortality_product.at(age, core::Gender::male) / male_count;
        }

        if (female_count > 0) {
            female_average_product =
                excess_mortality_product.at(age, core::Gender::female) / female_count;
        }

        const auto male_mortality =
            1.0 - (1.0 - death_rates.at(age, core::Gender::male)) / male_average_product;
        const auto female_mortality =
            1.0 - (1.0 - death_rates.at(age, core::Gender::female)) / female_average_product;

        residual_mortality.at(age, core::Gender::male) = clamp_probability(male_mortality);
        residual_mortality.at(age, core::Gender::female) = clamp_probability(female_mortality);
    }

    return residual_mortality;
}

double DemographicModule::calculate_excess_mortality_product(const Person &entity,
                                                             const DiseaseModule &disease_host) {
    auto product = 1.0;
    for (const auto &item : entity.diseases) {
        if (item.second.status == DiseaseStatus::active) {
            const auto excess_mortality = disease_host.get_excess_mortality(item.first, entity);
            product *= (1.0 - excess_mortality);
        }
    }

    return clamp_probability(product);
}

int DemographicModule::update_age_and_death_events(RuntimeContext &context,
                                                   const DiseaseModule &disease_host) {
    const auto max_age = static_cast<unsigned int>(context.inputs().settings().age_range().upper());
    auto number_of_deaths = 0;

    for (auto &entity : context.population()) {
        if (!entity.is_active()) {
            continue;
        }

        if (entity.age >= max_age) {
            entity.die(context.time_now());
            number_of_deaths++;
        } else {
            const auto residual_death_rate = get_residual_death_rate(entity.age, entity.gender);
            auto product = 1.0 - residual_death_rate;

            for (const auto &item : entity.diseases) {
                if (item.second.status == DiseaseStatus::active) {
                    const auto excess_mortality =
                        disease_host.get_excess_mortality(item.first, entity);
                    product *= (1.0 - excess_mortality);
                }
            }

            const auto death_probability = clamp_probability(1.0 - product);
            const auto hazard = context.random().next_double();
            if (hazard < death_probability) {
                entity.die(context.time_now());
                number_of_deaths++;
            }
        }

        if (entity.is_active()) {
            entity.age = entity.age + 1;
        }
    }

    return number_of_deaths;
}

void DemographicModule::initialise_region(RuntimeContext &context, Person &person, Random &random) {
    if (!context.inputs().project_requirements().demographics.region) {
        return;
    }

    if (region_prevalence_.empty()) {
        return;
    }

    const core::Identifier age_id("age_" + std::to_string(person.age));
    auto target_age_id = age_id;

    if (!region_prevalence_.contains(age_id)) {
        if (person.age == 0) {
            std::vector<std::string> available_ages;
            available_ages.reserve(region_prevalence_.size());
            for (const auto &[age_key, _] : region_prevalence_) {
                available_ages.push_back(age_key.to_string());
            }

            throw core::InternalError(fmt::format(
                "Region data for age_0 (newborns) is missing. Available age keys: [{}].",
                fmt::join(available_ages, ", ")));
        }

        int min_diff = std::numeric_limits<int>::max();
        bool found_closest = false;

        for (const auto &[age_key, _] : region_prevalence_) {
            const auto age_str = age_key.to_string();
            if (age_str.starts_with("age_")) {
                const auto age_val = std::stoi(age_str.substr(4));
                const auto diff = std::abs(static_cast<int>(person.age) - age_val);
                if (diff < min_diff) {
                    min_diff = diff;
                    target_age_id = age_key;
                    found_closest = true;
                }
            }
        }

        if (!found_closest) {
            throw core::InternalError(
                fmt::format("No valid region data found for age: {}.", person.age));
        }
    }

    if (!region_prevalence_.at(target_age_id).contains(person.gender)) {
        const std::string gender_str = (person.gender == core::Gender::male) ? "male" : "female";
        throw core::InternalError(fmt::format(
            "Gender '{}' not found in region_prevalence_ for age: {}.",
            gender_str, target_age_id.to_string()));
    }

    const auto &region_probs = region_prevalence_.at(target_age_id).at(person.gender);

    const auto rand_value = random.next_double();
    auto cumulative_prob = 0.0;

    for (const auto &[region_name, prob] : region_probs) {
        cumulative_prob += prob;
        if (rand_value < cumulative_prob) {
            person.region = region_name;
            return;
        }
    }

    throw core::InternalError(fmt::format(
        "Failed to assign region: cumulative probabilities do not sum to 1.0. "
        "Age: {}, Gender: {}, Cumulative sum: {}",
        target_age_id.to_string(), (person.gender == core::Gender::male) ? "male" : "female",
        cumulative_prob));
}

void DemographicModule::initialise_ethnicity(RuntimeContext &context, Person &person,
                                             Random &random) {
    if (!context.inputs().project_requirements().demographics.ethnicity) {
        return;
    }

    if (ethnicity_prevalence_.empty()) {
        return;
    }

    const core::Identifier age_group = person.age < 18 ? "Under18"_id : "Over18"_id;

    if (!ethnicity_prevalence_.contains(age_group)) {
        std::vector<std::string> available_age_groups;
        available_age_groups.reserve(ethnicity_prevalence_.size());
        for (const auto &[ag, _] : ethnicity_prevalence_) {
            available_age_groups.emplace_back(ag.to_string());
        }

        throw core::InternalError(fmt::format(
            "Age group '{}' not found in ethnicity_prevalence_ map. Available age groups: [{}].",
            age_group.to_string(), fmt::join(available_age_groups, ", ")));
    }

    if (!ethnicity_prevalence_.at(age_group).contains(person.gender)) {
        const std::string gender_str = (person.gender == core::Gender::male) ? "male" : "female";
        throw core::InternalError(fmt::format(
            "Gender '{}' not found in ethnicity_prevalence_ for age group: {}.",
            gender_str, age_group.to_string()));
    }

    if (!ethnicity_prevalence_.at(age_group).at(person.gender).contains(person.region)) {
        throw core::InternalError(fmt::format(
            "Region '{}' not found in ethnicity_prevalence_ for age group: {} and gender: {}.",
            person.region, age_group.to_string(),
            (person.gender == core::Gender::male) ? "male" : "female"));
    }

    const auto &ethnicity_probs =
        ethnicity_prevalence_.at(age_group).at(person.gender).at(person.region);

    const auto rand_value = random.next_double();
    auto cumulative_prob = 0.0;

    for (const auto &[ethnicity_name, prob] : ethnicity_probs) {
        cumulative_prob += prob;
        if (rand_value < cumulative_prob) {
            if (ethnicity_name == "1") {
                person.ethnicity = "ethnicity1";
            } else if (ethnicity_name == "2") {
                person.ethnicity = "ethnicity2";
            } else if (ethnicity_name == "3") {
                person.ethnicity = "ethnicity3";
            } else if (ethnicity_name == "4") {
                person.ethnicity = "ethnicity4";
            } else {
                person.ethnicity = ethnicity_name;
            }
            return;
        }
    }

    throw core::InternalError(fmt::format(
        "Failed to assign ethnicity: cumulative probabilities do not sum to 1.0. "
        "Age group: {}, Gender: {}, Region: {}, Cumulative sum: {}",
        age_group.to_string(), (person.gender == core::Gender::male) ? "male" : "female",
        person.region, cumulative_prob));
}

void DemographicModule::set_region_prevalence(
    const std::map<core::Identifier, std::map<core::Gender, std::map<std::string, double>>>
        &region_data) {
    region_prevalence_ = region_data;
}

void DemographicModule::set_ethnicity_prevalence(
    const std::map<core::Identifier,
                   std::map<core::Gender, std::map<std::string, std::map<std::string, double>>>>
        &ethnicity_data) {
    ethnicity_prevalence_ = ethnicity_data;
}

std::unique_ptr<DemographicModule> build_population_module(Repository &repository,
                                                           const ModelInput &config) {
    auto pop_data = std::map<int, std::map<int, PopulationRecord>>{};

    const auto min_time = config.start_time();
    const auto max_time = config.stop_time();
    const auto time_filter = [&min_time, &max_time](unsigned int value) {
        return value >= min_time && value <= max_time;
    };

    auto pop = repository.manager().get_population(config.settings().country(), time_filter);
    for (auto &item : pop) {
        pop_data[item.at_time].emplace(item.with_age,
                                       PopulationRecord(item.with_age, item.males, item.females));
    }

    const auto births =
        repository.manager().get_birth_indicators(config.settings().country(), time_filter);
    const auto deaths = repository.manager().get_mortality(config.settings().country(), time_filter);
    auto life_table = detail::StoreConverter::to_life_table(births, deaths);

    auto demographic_module =
        std::make_unique<DemographicModule>(std::move(pop_data), std::move(life_table));

    const auto &region_data = repository.get_region_prevalence();
    if (!region_data.empty()) {
        demographic_module->set_region_prevalence(region_data);
    }

    const auto &ethnicity_data = repository.get_ethnicity_prevalence();
    if (!ethnicity_data.empty()) {
        demographic_module->set_ethnicity_prevalence(ethnicity_data);
    }

    return demographic_module;
}
} // namespace hgps