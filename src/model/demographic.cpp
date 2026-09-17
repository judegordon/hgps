#include "demographic.h"

#include "core/parallel.h"
#include "diagnostics/internal_error.h"
#include "random/categorical.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {
namespace {

/// The age key the region prevalence data uses, e.g. "age_37".
core::Identifier age_key(unsigned int age) {
    return core::Identifier{fmt::format("age_{}", age)};
}

/// The ethnicity data is grouped into two age bands only.
core::Identifier ethnicity_age_group(unsigned int age) {
    return age < 18 ? core::Identifier{"under18"} : core::Identifier{"over18"};
}

/// The ethnicity CSVs name categories "1".."4"; the model coefficients call them
/// "ethnicity1".."ethnicity4".
std::string ethnicity_category_name(const std::string &raw) {
    if (raw.size() == 1 && raw[0] >= '1' && raw[0] <= '9') {
        return fmt::format("ethnicity{}", raw);
    }
    return raw;
}

} // namespace

DemographicModule::DemographicModule(std::map<int, std::map<int, PopulationRecord>> population_data,
                                     LifeTable life_table)
    : population_data_{std::move(population_data)}, life_table_{std::move(life_table)} {
    if (population_data_.empty()) {
        if (!life_table_.empty()) {
            throw std::invalid_argument("empty population and life table content mismatch.");
        }
        return;
    }

    if (life_table_.empty()) {
        throw std::invalid_argument("population and empty life table content mismatch.");
    }

    const auto first_entry = population_data_.begin();
    const auto time_range =
        core::IntegerInterval(first_entry->first, population_data_.rbegin()->first);

    core::IntegerInterval age_range{};
    if (!first_entry->second.empty()) {
        age_range = core::IntegerInterval(first_entry->second.begin()->first,
                                          first_entry->second.rbegin()->first);
    }

    if (time_range != life_table_.time_limits()) {
        throw std::invalid_argument(
            fmt::format("Population and life table time limits mismatch: {} vs {}.",
                        time_range.to_string(), life_table_.time_limits().to_string()));
    }

    if (age_range != life_table_.age_limits()) {
        throw std::invalid_argument(
            fmt::format("Population and life table age limits mismatch: {} vs {}.",
                        age_range.to_string(), life_table_.age_limits().to_string()));
    }

    initialise_birth_rates();
}

std::size_t DemographicModule::get_total_population_size(int time_year) const noexcept {
    const auto found = population_data_.find(time_year);
    if (found == population_data_.end()) {
        return 0;
    }

    // Ascending age order, because the map is keyed by age.
    double total = 0.0;
    for (const auto &[age, record] : found->second) {
        total += static_cast<double>(record.total());
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
    return population_data_.at(time_year);
}

std::map<int, GenderValue<double>>
DemographicModule::get_age_gender_distribution(int time_year) const {
    std::map<int, GenderValue<double>> result;

    const auto found = population_data_.find(time_year);
    if (found == population_data_.end() || found->second.empty()) {
        return result;
    }

    const auto total = static_cast<double>(get_total_population_size(time_year));
    if (total <= 0.0) {
        return result;
    }

    for (const auto &[age, record] : found->second) {
        result.emplace(age, GenderValue<double>{static_cast<double>(record.males) / total,
                                                static_cast<double>(record.females) / total});
    }

    return result;
}

GenderValue<double> DemographicModule::get_birth_rate(int time_year) const noexcept {
    if (birth_rates_.contains(time_year)) {
        return GenderValue<double>{birth_rates_.at(time_year, core::Gender::male),
                                   birth_rates_.at(time_year, core::Gender::female)};
    }
    return GenderValue<double>{0.0, 0.0};
}

double DemographicModule::get_residual_death_rate(int age, core::Gender gender) const noexcept {
    if (residual_death_rates_.contains(age, gender)) {
        return residual_death_rates_.at(age, gender);
    }
    return 0.0;
}

void DemographicModule::initialise_birth_rates() {
    birth_rates_ = create_integer_gender_table<double>(life_table_.time_limits());

    for (int year = life_table_.time_limits().lower(); year <= life_table_.time_limits().upper();
         ++year) {
        const auto &births = life_table_.get_births_at(year);
        const auto population_size = static_cast<double>(get_total_population_size(year));
        if (population_size <= 0.0) {
            throw diag::InternalError(
                fmt::format("no population data for year {}, so its birth rate cannot be "
                            "computed; the constructor's limit check should have caught this",
                            year));
        }

        // The sex ratio is males per 100 females, as the UN data gives it.
        const double ratio = static_cast<double>(births.sex_ratio);
        const double number = static_cast<double>(births.number);

        birth_rates_.at(year, core::Gender::male) =
            number * ratio / (1.0 + ratio) / population_size;
        birth_rates_.at(year, core::Gender::female) = number / (1.0 + ratio) / population_size;
    }
}

void DemographicModule::initialise_population(RuntimeContext &context) {
    const auto distribution = get_age_gender_distribution(context.start_time());
    if (distribution.empty()) {
        throw diag::InternalError(
            fmt::format("no population distribution for the start year {}", context.start_time()));
    }

    const auto population_size = static_cast<int>(context.population().size());
    const auto entry_total = static_cast<int>(distribution.size());

    int index = 0;
    int entry_count = 1;
    for (const auto &[age, share] : distribution) {
        auto males = static_cast<int>(std::round(population_size * share.male));
        auto females = static_cast<int>(std::round(population_size * share.female));
        auto difference = population_size - (index + males + females);

        // Rounding leaves the cohort a few people short or long; the last age band absorbs the
        // remainder, split between the sexes with the larger share taking the odd one.
        if (entry_count == entry_total && difference > 0) {
            const auto half = difference / 2;
            males += half;
            females += half;
            if (share.male > share.female) {
                males += difference - (half * 2);
            } else {
                females += difference - (half * 2);
            }
        } else if (difference < 0) {
            difference = -difference;
            if (share.male > share.female) {
                females -= difference;
                if (females < 0) {
                    males += females;
                }
            } else {
                males -= difference;
                if (males < 0) {
                    females += males;
                }
            }

            males = std::max(0, males);
            females = std::max(0, females);
        }

        // Males then females, each in slot order: the draw order for region and ethnicity is
        // therefore a function of the distribution, not of anything incidental.
        for (int i = 0; i < males; ++i) {
            auto &person = context.population()[static_cast<std::size_t>(index + i)];
            person.age = static_cast<unsigned int>(age);
            person.gender = core::Gender::male;
            initialise_region(context, person);
            initialise_ethnicity(context, person);
        }
        index += males;

        for (int i = 0; i < females; ++i) {
            auto &person = context.population()[static_cast<std::size_t>(index + i)];
            person.age = static_cast<unsigned int>(age);
            person.gender = core::Gender::female;
            initialise_region(context, person);
            initialise_ethnicity(context, person);
        }
        index += females;

        ++entry_count;
    }

    if (index != population_size) {
        throw diag::InternalError(
            fmt::format("the initial cohort was filled to {} of {} people; the rounding "
                        "adjustment is wrong",
                        index, population_size));
    }
}

void DemographicModule::update_population(RuntimeContext &context,
                                          const ExcessMortalityHost &disease_host,
                                          sim::ScenarioJournal &journal) {
    const auto initial_population_size = context.population().current_active_size();
    const auto expected_population_size = get_total_population_size(context.time_now());
    const auto expected_deaths = get_total_deaths(context.time_now());

    // Residual mortality first: it is what the death events below are drawn against. The baseline
    // computes it on a std::async future concurrently with querying the population; here it is
    // sequential, which is also what makes the result independent of scheduling.
    update_residual_mortality(context, disease_host, journal);

    const auto deaths = update_age_and_death_events(context, disease_host);

    // Births use last year's rate against this year's starting population, as upstream.
    const auto birth_rate = get_birth_rate(context.time_now() - 1);
    const auto boys =
        static_cast<std::size_t>(birth_rate.male * static_cast<double>(initial_population_size));
    const auto girls =
        static_cast<std::size_t>(birth_rate.female * static_cast<double>(initial_population_size));

    const auto now = static_cast<unsigned int>(context.time_now());
    context.population().add_newborn_babies(boys, core::Gender::male, now);
    context.population().add_newborn_babies(girls, core::Gender::female, now);

    // Newborns need a region and an ethnicity before the risk-factor models run.
    for (auto &person : context.population()) {
        if (person.is_active() && person.age == 0) {
            initialise_region(context, person);
            initialise_ethnicity(context, person);
        }
    }

    const double simulated_death_rate =
        initial_population_size == 0
            ? 0.0
            : static_cast<double>(deaths) * 1000.0 / static_cast<double>(initial_population_size);
    const double expected_death_rate =
        expected_population_size == 0
            ? 0.0
            : expected_deaths * 1000.0 / static_cast<double>(expected_population_size);

    context.metrics()["SimulatedDeathRate"] = simulated_death_rate;
    context.metrics()["ExpectedDeathRate"] = expected_death_rate;
    context.metrics()["DeathRateDeltaPercent"] =
        expected_death_rate == 0.0 ? 0.0
                                   : 100.0 * (simulated_death_rate / expected_death_rate - 1.0);
}

void DemographicModule::update_residual_mortality(RuntimeContext &context,
                                                  const ExcessMortalityHost &disease_host,
                                                  sim::ScenarioJournal &journal) {
    if (context.scenario().type() == sim::ScenarioType::baseline) {
        residual_death_rates_ = calculate_residual_mortality(context, disease_host);
        journal.record_residual_mortality(context.current_run(), context.time_now(),
                                          residual_death_rates_);
        return;
    }

    // The intervention reuses the baseline's table rather than recomputing it from its own
    // population, so the two futures differ only by the policy (ADR 0009).
    residual_death_rates_ =
        journal.replay_residual_mortality(context.current_run(), context.time_now());
}

GenderTable<int, double> DemographicModule::create_death_rates_table(int time_year) const {
    const auto &population = population_data_.at(time_year);
    const auto &mortality = life_table_.get_mortalities_at(time_year);

    auto death_rates = create_integer_gender_table<double>(life_table_.age_limits());
    for (int age = life_table_.age_limits().lower(); age <= life_table_.age_limits().upper();
         ++age) {
        const auto &deaths = mortality.at(age);
        const auto &people = population.at(age);

        // double throughout, where the baseline divides floats and clamps against 1.0f — a
        // needless narrowing on a quantity that feeds every death draw (audit N-13).
        const double male_rate =
            people.males <= 0.0F
                ? 0.0
                : std::min(static_cast<double>(deaths.male) / static_cast<double>(people.males),
                           1.0);
        const double female_rate =
            people.females <= 0.0F
                ? 0.0
                : std::min(static_cast<double>(deaths.female) / static_cast<double>(people.females),
                           1.0);

        death_rates.at(age, core::Gender::male) = male_rate;
        death_rates.at(age, core::Gender::female) = female_rate;
    }

    return death_rates;
}

double DemographicModule::calculate_excess_mortality_product(
    const Person &person, const ExcessMortalityHost &disease_host) {
    // person.diseases is a std::map, so this product is taken in disease-code order.
    double product = 1.0;
    for (const auto &[disease, state] : person.diseases) {
        if (state.status == DiseaseStatus::active) {
            product *= 1.0 - disease_host.excess_mortality(disease, person);
        }
    }

    return std::clamp(product, 0.0, 1.0);
}

GenderTable<int, double>
DemographicModule::calculate_residual_mortality(RuntimeContext &context,
                                                const ExcessMortalityHost &disease_host) const {
    const auto &population = context.population();

    // The reduction the baseline does with a mutex-guarded `+=` inside a parallel loop, which is
    // race-free but not order-stable (audit N-7). Here each block accumulates in index order and
    // the partials combine in block order, so the sums are identical at any thread count
    // (determinism clause D5). The map function is a pure function of one person and draws
    // nothing, which is what makes it eligible to run in a parallel region at all (D3).
    struct Accumulator {
        std::map<std::pair<int, core::Gender>, double> product;
        std::map<std::pair<int, core::Gender>, int> count;
    };

    const auto combine = [](Accumulator left, Accumulator right) {
        for (const auto &[key, value] : right.product) {
            left.product[key] += value;
        }
        for (const auto &[key, value] : right.count) {
            left.count[key] += value;
        }
        return left;
    };

    const auto totals = core::parallel::reduce_ordered(
        population.size(), Accumulator{},
        [&population, &disease_host](std::size_t index) {
            Accumulator single;
            const auto &person = population[index];
            if (!person.is_active()) {
                return single;
            }

            const auto key = std::make_pair(static_cast<int>(person.age), person.gender);
            single.product[key] = calculate_excess_mortality_product(person, disease_host);
            single.count[key] = 1;
            return single;
        },
        combine);

    const auto death_rates = create_death_rates_table(context.time_now());
    auto residual = create_integer_gender_table<double>(life_table_.age_limits());

    for (int age = life_table_.age_limits().lower(); age <= life_table_.age_limits().upper();
         ++age) {
        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            const auto key = std::make_pair(age, gender);

            // With nobody of this age and sex alive, the average product is 1: no modelled
            // disease mortality to subtract, so the residual is the whole death rate.
            double average = 1.0;
            const auto count = totals.count.find(key);
            if (count != totals.count.end() && count->second > 0) {
                average = totals.product.at(key) / static_cast<double>(count->second);
            }

            const double mortality = 1.0 - (1.0 - death_rates.at(age, gender)) / average;
            residual.at(age, gender) = std::clamp(mortality, 0.0, 1.0);
        }
    }

    return residual;
}

int DemographicModule::update_age_and_death_events(
    RuntimeContext &context, const ExcessMortalityHost &disease_host) const {
    const auto max_age = static_cast<unsigned int>(context.age_range().upper());
    int deaths = 0;

    // Serial, in slot order, because it draws. Determinism clause D3 makes this structural: a
    // parallel version of this loop would throw on its first draw.
    auto &population = context.population();
    for (std::size_t index = 0; index < population.size(); ++index) {
        auto &person = population[index];
        if (!person.is_active()) {
            continue;
        }

        if (person.age >= max_age) {
            population.mark_died(index, static_cast<unsigned int>(context.time_now()));
            ++deaths;
            continue;
        }

        double product = 1.0 - get_residual_death_rate(static_cast<int>(person.age), person.gender);
        for (const auto &[disease, state] : person.diseases) {
            if (state.status == DiseaseStatus::active) {
                product *= 1.0 - disease_host.excess_mortality(disease, person);
            }
        }

        const double death_probability = 1.0 - product;
        if (context.random().next_double() < death_probability) {
            population.mark_died(index, static_cast<unsigned int>(context.time_now()));
            ++deaths;
            continue;
        }

        person.age += 1;
    }

    return deaths;
}

void DemographicModule::initialise_region(RuntimeContext &context, Person &person) const {
    if (!context.inputs().project_requirements().demographics.region) {
        return;
    }
    if (region_prevalence_.empty()) {
        throw diag::InternalError(
            "the project requires regions but no region prevalence data was loaded");
    }

    const auto exact = age_key(person.age);
    auto target = exact;

    if (!region_prevalence_.contains(exact)) {
        // A newborn with no row of their own cannot borrow another age's: age 0 is where the
        // region distribution differs most, and the baseline throws here for the same reason.
        if (person.age == 0) {
            throw diag::InternalError(
                "the region data has no row for age 0, so newborns cannot be assigned a region");
        }

        // Otherwise the nearest age present, preferring the lower on a tie so the choice does not
        // depend on map iteration direction.
        int best_difference = std::numeric_limits<int>::max();
        bool found = false;
        for (const auto &[key, _] : region_prevalence_) {
            const auto &text = key.to_string();
            if (!text.starts_with("age_")) {
                continue;
            }
            const int age = std::stoi(text.substr(4));
            const int difference = std::abs(static_cast<int>(person.age) - age);
            if (difference < best_difference) {
                best_difference = difference;
                target = key;
                found = true;
            }
        }

        if (!found) {
            throw diag::InternalError(
                fmt::format("the region data has no usable age row for age {}", person.age));
        }
    }

    const auto &by_gender = region_prevalence_.at(target);
    const auto shares = by_gender.find(person.gender);
    if (shares == by_gender.end()) {
        throw diag::InternalError(
            fmt::format("the region data has no {} row for {}",
                        person.gender == core::Gender::male ? "male" : "female",
                        target.to_string()));
    }

    // Ordered by region name, and sampled through Categorical, which normalises the shares and
    // cannot be built from an unordered container (determinism clause D4).
    const auto distribution = rng::Categorical<std::string>::from_ordered_map(shares->second);
    person.region = distribution.sample(context.random());
}

void DemographicModule::initialise_ethnicity(RuntimeContext &context, Person &person) const {
    if (!context.inputs().project_requirements().demographics.ethnicity) {
        return;
    }
    if (ethnicity_prevalence_.empty()) {
        throw diag::InternalError(
            "the project requires ethnicity but no ethnicity prevalence data was loaded");
    }

    const auto group = ethnicity_age_group(person.age);
    const auto by_group = ethnicity_prevalence_.find(group);
    if (by_group == ethnicity_prevalence_.end()) {
        throw diag::InternalError(
            fmt::format("the ethnicity data has no '{}' age group", group.to_string()));
    }

    const auto by_gender = by_group->second.find(person.gender);
    if (by_gender == by_group->second.end()) {
        throw diag::InternalError(
            fmt::format("the ethnicity data has no {} row for age group '{}'",
                        person.gender == core::Gender::male ? "male" : "female",
                        group.to_string()));
    }

    const auto shares = by_gender->second.find(person.region);
    if (shares == by_gender->second.end()) {
        throw diag::InternalError(
            fmt::format("the ethnicity data has no row for region '{}' in age group '{}'",
                        person.region, group.to_string()));
    }

    const auto distribution = rng::Categorical<std::string>::from_ordered_map(shares->second);
    person.ethnicity = ethnicity_category_name(distribution.sample(context.random()));
}

void DemographicModule::set_region_prevalence(RegionPrevalence region_data) {
    region_prevalence_ = std::move(region_data);
}

void DemographicModule::set_ethnicity_prevalence(EthnicityPrevalence ethnicity_data) {
    ethnicity_prevalence_ = std::move(ethnicity_data);
}

} // namespace hgps::model
