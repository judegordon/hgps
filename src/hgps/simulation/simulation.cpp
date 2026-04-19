#include "simulation.h"
#include "hgps_core/utils/thread_util.h"
#include "data/converter.h"
#include "events/info_message.h"
#include "utils/mt_random.h"
#include "models/static_linear_model.h"
#include "events/sync_message.h"

#include <algorithm>
#include <fmt/format.h>
#include <memory>
#include <oneapi/tbb/parallel_for_each.h>
#include <stdexcept>

namespace { // anonymous namespace

using NetImmigrationMessage = hgps::SyncDataMessage<hgps::IntegerAgeGenderTable>;

} // anonymous namespace

namespace hgps {

Simulation::Simulation(SimulationModuleFactory &factory,
                       std::shared_ptr<const EventAggregator> bus,
                       std::shared_ptr<const ModelInput> inputs,
                       std::unique_ptr<Scenario> scenario,
                       SyncChannel &sync_channel)
    : context_{std::move(bus), std::move(inputs), std::move(scenario), sync_channel} {

    auto ses_base = factory.create(SimulationModuleType::SES, context_.inputs());
    auto dem_base = factory.create(SimulationModuleType::Demographic, context_.inputs());
    auto risk_base = factory.create(SimulationModuleType::RiskFactor, context_.inputs());
    auto disease_base = factory.create(SimulationModuleType::Disease, context_.inputs());
    auto analysis_base = factory.create(SimulationModuleType::Analysis, context_.inputs());

    ses_ = std::static_pointer_cast<UpdatableModule>(ses_base);
    demographic_ = std::static_pointer_cast<DemographicModule>(dem_base);
    risk_factor_ = std::static_pointer_cast<RiskFactorHostModule>(risk_base);
    disease_ = std::static_pointer_cast<DiseaseModule>(disease_base);
    analysis_ = std::static_pointer_cast<UpdatableModule>(analysis_base);
}

void Simulation::setup_run(unsigned int run_number, unsigned int run_seed) noexcept {
    context_.set_current_run(run_number);
    context_.random().seed(run_seed);
}

adevs::Time Simulation::init(adevs::SimEnv<int> *env) {
    auto start = std::chrono::steady_clock::now();
    const auto &inputs = context_.inputs();
    auto world_time = inputs.start_time();
    context_.metrics().clear();
    context_.scenario().clear();
    context_.set_current_time(world_time);
    end_time_ = adevs::Time(inputs.stop_time(), 0);

    initialise_population();

    auto stop = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(stop - start);

    auto message =
        fmt::format("[{:4},{}] population size: {}, elapsed: {}ms", env->now().real,
                    env->now().logical, context_.population().initial_size(), elapsed.count());
    context_.publish(std::make_unique<InfoEventMessage>(
        name(), ModelAction::start, context_.current_run(), context_.time_now(), message));

    return env->now() + adevs::Time(world_time, 0);
}

adevs::Time Simulation::update(adevs::SimEnv<int> *env) {
    if (env->now() < end_time_) {
        auto start = std::chrono::steady_clock::now();
        context_.metrics().reset();

        auto world_time = env->now() + adevs::Time(1, 0);
        auto time_year = world_time.real;
        context_.set_current_time(time_year);
        update_population();

        auto stop = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double, std::milli>(stop - start);

        auto message = fmt::format("[{:4},{}], elapsed: {}ms", env->now().real, env->now().logical,
                                   elapsed.count());
        context_.publish(std::make_unique<InfoEventMessage>(
            name(), ModelAction::update, context_.current_run(), context_.time_now(), message));

        return world_time;
    }

    env->remove(this);
    return adevs_inf<adevs::Time>();
}

adevs::Time Simulation::update(adevs::SimEnv<int> * /*env*/, std::vector<int> & /*x*/) {
    return adevs_inf<adevs::Time>();
}

void Simulation::fini(adevs::Time clock) {
    auto message = fmt::format("[{:4},{}] clear up resources.", clock.real, clock.logical);
    context_.publish(std::make_unique<InfoEventMessage>(
        name(), ModelAction::stop, context_.current_run(), context_.time_now(), message));
}

void Simulation::initialise_population() {
    const auto &inputs = context_.inputs();

    auto model_start_year = inputs.start_time();
    auto total_year_pop_size = demographic_->get_total_population_size(model_start_year);
    float size_fraction = inputs.settings().size_fraction();
    auto virtual_pop_size = static_cast<int>(size_fraction * total_year_pop_size);

    context_.reset_population(virtual_pop_size);

    demographic_->initialise_population(context_);
    ses_->initialise_population(context_);
    risk_factor_->initialise_population(context_);
    disease_->initialise_population(context_);
    analysis_->initialise_population(context_);
}

void Simulation::update_population() {
    demographic_->update_population(context_, *disease_);
    update_net_immigration();
    ses_->update_population(context_);
    risk_factor_->update_population(context_);
    disease_->update_population(context_);
    analysis_->update_population(context_);
}

void Simulation::update_net_immigration() {
    auto net_immigration = get_net_migration();

    auto start_age = context_.age_range().lower();
    auto end_age = context_.age_range().upper();
    for (int age = start_age; age <= end_age; age++) {
        auto male_net_value = net_immigration.at(age, core::Gender::male);
        apply_net_migration(male_net_value, age, core::Gender::male);

        auto female_net_value = net_immigration.at(age, core::Gender::female);
        apply_net_migration(female_net_value, age, core::Gender::female);
    }

    if (context_.scenario().type() == ScenarioType::baseline) {
        context_.sync_channel().send(std::make_unique<NetImmigrationMessage>(
            context_.current_run(), context_.time_now(), std::move(net_immigration)));
    }
}

IntegerAgeGenderTable Simulation::get_current_expected_population() const {
    const auto &inputs = context_.inputs();
    auto sim_start_time = context_.start_time();
    auto total_initial_population = demographic_->get_total_population_size(sim_start_time);
    float size_fraction = inputs.settings().size_fraction();
    auto start_population_size = static_cast<int>(size_fraction * total_initial_population);

    const auto &current_population_table =
        demographic_->get_population_distribution(context_.time_now());
    auto expected_population = create_age_gender_table<int>(context_.age_range());
    auto start_age = context_.age_range().lower();
    auto end_age = context_.age_range().upper();
    for (int age = start_age; age <= end_age; age++) {
        const auto &age_info = current_population_table.at(age);
        expected_population.at(age, core::Gender::male) = static_cast<int>(
            std::round(age_info.males * start_population_size / total_initial_population));

        expected_population.at(age, core::Gender::female) = static_cast<int>(
            std::round(age_info.females * start_population_size / total_initial_population));
    }

    return expected_population;
}

IntegerAgeGenderTable Simulation::get_current_simulated_population() {
    auto simulated_population = create_age_gender_table<int>(context_.age_range());
    auto &pop = context_.population();
    auto count_mutex = std::mutex{};
    tbb::parallel_for_each(pop.cbegin(), pop.cend(), [&](const auto &entity) {
        if (!entity.is_active()) {
            return;
        }

        auto lock = std::unique_lock{count_mutex};
        simulated_population.at(entity.age, entity.gender)++;
    });

    return simulated_population;
}

void Simulation::apply_net_migration(int net_value, unsigned int age, const core::Gender &gender) {
    if (net_value > 0) {
        auto &pop = context_.population();
        auto similar_indices = core::find_index_of_all(pop, [&](const Person &entity) {
            return entity.is_active() && entity.age == age && entity.gender == gender;
        });

        if (!similar_indices.empty()) {
            std::sort(similar_indices.begin(), similar_indices.end());

            for (auto trial = 0; trial < net_value; trial++) {
                auto index =
                    context_.random().next_int(static_cast<int>(similar_indices.size()) - 1);
                const auto &source = pop.at(similar_indices.at(index));
                context_.population().add(partial_clone_entity(source), context_.time_now());
            }
        }
    } else if (net_value < 0) {
        auto net_value_counter = net_value;
        for (auto &entity : context_.population()) {
            if (!entity.is_active()) {
                continue;
            }

            if (entity.age == age && entity.gender == gender) {
                entity.emigrate(context_.time_now());
                net_value_counter++;
                if (net_value_counter == 0) {
                    break;
                }
            }
        }
    }
}

hgps::IntegerAgeGenderTable Simulation::get_net_migration() {
    if (context_.scenario().type() == ScenarioType::baseline) {
        return create_net_migration();
    }

    auto message = context_.sync_channel().try_receive(context_.sync_timeout_millis());
    if (message.has_value()) {
        auto &basePtr = message.value();
        auto *messagePrt = dynamic_cast<NetImmigrationMessage *>(basePtr.get());
        if (messagePrt) {
            return messagePrt->data();
        }

        throw std::runtime_error(
            "Simulation out of sync, failed to receive a net immigration message");
    }

    throw std::runtime_error(fmt::format(
        "Simulation out of sync, receive net immigration message has timed out after {} ms.",
        context_.sync_timeout_millis()));
}

hgps::IntegerAgeGenderTable Simulation::create_net_migration() {
    auto expected_future = core::run_async(&Simulation::get_current_expected_population, this);
    auto simulated_population = get_current_simulated_population();
    auto net_emigration = create_age_gender_table<int>(context_.age_range());
    auto start_age = context_.age_range().lower();
    auto end_age = context_.age_range().upper();
    auto expected_population = expected_future.get();
    auto net_value = 0;
    for (int age = start_age; age <= end_age; age++) {
        net_value = expected_population.at(age, core::Gender::male) -
                    simulated_population.at(age, core::Gender::male);
        net_emigration.at(age, core::Gender::male) = net_value;

        net_value = expected_population.at(age, core::Gender::female) -
                    simulated_population.at(age, core::Gender::female);
        net_emigration.at(age, core::Gender::female) = net_value;
    }

    return net_emigration;
}

Person Simulation::partial_clone_entity(const Person &source) noexcept {
    // Population::add(...) assigns the final slot-based ID.
    auto clone = Person{0};
    clone.age = source.age;
    clone.gender = source.gender;
    clone.region = source.region;
    clone.ethnicity = source.ethnicity;
    clone.ses = source.ses;
    clone.sector = source.sector;
    clone.income = source.income;

    for (const auto &item : source.risk_factors) {
        clone.risk_factors.emplace(item.first, item.second);
    }

    for (const auto &item : source.diseases) {
        clone.diseases.emplace(item.first, item.second.clone());
    }

    return clone;
}

} // namespace hgps