// Sector and income: the urban/rural draw, the two income models, and the rank-bucket split that
// turns a continuous income into a category and into an adjustment stratum.
//
// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: StaticLinearModel::{initialise_sector, update_sector, initialise_income, update_income,
//         initialise_categorical_income, calculate_continuous_income,
//         calculate_income_percentile_thresholds} and the anonymous-namespace
//         assign_equal_rank_buckets / assign_income_categories_equal_split in
//         src/HealthGPS/static_linear_model.cpp.
#include "static_linear_model.h"

#include "diagnostics/internal_error.h"
#include "model/runtime_context.h"
#include "random/categorical.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const core::Identifier kIncome{"income"};
const core::Identifier kUnder18{"under18"};
const core::Identifier kOver18{"over18"};
const core::Identifier kStdDev{"stddev"};
const core::Identifier kMin{"min"};
const core::Identifier kMax{"max"};

double coefficient_or(const LinearModelParams &model, const core::Identifier &key,
                      double fallback) {
    const auto found = model.coefficients.find(key);
    return found == model.coefficients.end() ? fallback : found->second;
}

} // namespace

std::vector<double> StaticLinearModel::softmax(std::span<const double> logits) {
    if (logits.empty()) {
        throw diag::InternalError("a softmax needs at least one logit");
    }

    // The maximum is subtracted before exponentiating. The baseline exponentiates the raw logits,
    // which overflows to infinity for a logit above about 709 and then divides infinity by
    // infinity, giving NaN probabilities and a CDF that never reaches the draw. Subtracting the
    // maximum is the standard remedy and is exactly equivalent for small logits, because the
    // constant cancels in the ratio.
    const double largest = *std::max_element(logits.begin(), logits.end());

    std::vector<double> weights;
    weights.reserve(logits.size());
    double total = 0.0;
    for (const double logit : logits) {
        const double weight = std::exp(logit - largest);
        weights.push_back(weight);
        total += weight;
    }

    if (!(total > 0.0) || !std::isfinite(total)) {
        throw diag::InternalError(
            fmt::format("softmax weights sum to {}, which cannot be normalised", total));
    }

    for (auto &weight : weights) {
        weight /= total;
    }

    return weights;
}

std::vector<std::pair<std::size_t, std::size_t>>
StaticLinearModel::equal_rank_buckets(const Population &population, std::size_t buckets) {
    if (buckets < 1) {
        return {};
    }

    std::vector<std::pair<double, std::size_t>> ranked;
    ranked.reserve(population.size());
    for (std::size_t index = 0; index < population.size(); ++index) {
        const auto &person = population[index];
        if (!person.is_active()) {
            continue;
        }
        const auto found = person.risk_factors.find(kIncome);
        if (found == person.risk_factors.end()) {
            continue;
        }
        ranked.emplace_back(found->second, index);
    }

    if (ranked.empty()) {
        return {};
    }

    // Ties are broken by slot index, so the order is total and the bucket a person lands in does
    // not depend on the sort's stability. The baseline breaks ties the same way, and has to:
    // income takes very few distinct values in the categorical shape.
    std::sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        if (left.first != right.first) {
            return left.first < right.first;
        }
        return left.second < right.second;
    });

    // Equal-population buckets by rank rather than by threshold. Thresholds leave the top bucket
    // empty whenever the highest value is shared, which it often is.
    std::vector<std::pair<std::size_t, std::size_t>> assignment;
    assignment.reserve(ranked.size());
    for (std::size_t rank = 0; rank < ranked.size(); ++rank) {
        assignment.emplace_back(ranked[rank].second, (rank * buckets) / ranked.size());
    }

    return assignment;
}

std::vector<double> StaticLinearModel::income_percentile_thresholds(const Population &population,
                                                                     std::size_t buckets) {
    if (buckets < 2) {
        return {};
    }

    std::vector<double> incomes;
    incomes.reserve(population.size());
    for (const auto &person : population) {
        if (!person.is_active()) {
            continue;
        }
        const auto found = person.risk_factors.find(kIncome);
        if (found != person.risk_factors.end()) {
            incomes.push_back(found->second);
        }
    }

    if (incomes.empty()) {
        throw diag::InternalError(
            "no income values in the population, so no income thresholds can be computed");
    }

    std::sort(incomes.begin(), incomes.end());

    // The (i+1)/buckets quantile by nearest rank, which is what the baseline computes for the
    // tertile and quartile cases it special-cases; generalised here so a five-category layout
    // gets the same rule rather than a different code path.
    std::vector<double> thresholds;
    thresholds.reserve(buckets - 1);
    const auto last = incomes.size() - 1;
    for (std::size_t i = 0; i + 1 < buckets; ++i) {
        const double quantile =
            static_cast<double>(i + 1) / static_cast<double>(buckets);
        auto index = static_cast<std::size_t>(
            std::llround(static_cast<double>(last) * quantile));
        index = std::min(index, last);
        thresholds.push_back(incomes[index]);
    }

    return thresholds;
}

void StaticLinearModel::assign_income_categories(Population &population,
                                                  const core::IncomeCategoryLayout &layout) {
    for (const auto &[index, bucket] : equal_rank_buckets(population, layout.count)) {
        population[index].income = core::income_from_equal_split_bucket(bucket, layout);
    }
}

void StaticLinearModel::assign_adjustment_strata(Population &population, std::size_t buckets) {
    if (buckets < 2) {
        return;
    }

    // Cleared first, so a person who no longer has an income — or a stale flag from last year —
    // cannot be silently counted into a stratum they are not in.
    for (auto &person : population) {
        person.has_income_adjustment_stratum = false;
        person.income_adjustment_stratum = 0;
    }

    for (const auto &[index, bucket] : equal_rank_buckets(population, buckets)) {
        population[index].income_adjustment_stratum = bucket;
        population[index].has_income_adjustment_stratum = true;
    }
}

void StaticLinearModel::initialise_sector(Person &person, rng::RandomSource &random) const {
    if (parameters_->rural_prevalence.empty() || !person.is_active()) {
        return;
    }

    const auto group = person.age < 18 ? kUnder18 : kOver18;
    const auto found = parameters_->rural_prevalence.find(group);
    if (found == parameters_->rural_prevalence.end()) {
        throw diag::InternalError(
            fmt::format("the model's RuralPrevalence has no '{}' row", group.to_string()));
    }

    const double prevalence = found->second.at(person.gender);
    person.sector =
        random.next_double() < prevalence ? core::Sector::rural : core::Sector::urban;
}

void StaticLinearModel::update_sector(Person &person, rng::RandomSource &random) const {
    if (parameters_->rural_prevalence.empty()) {
        return;
    }

    // Only the transition out of the rural sector at 18, which is the one the data describes.
    if (person.age != 18 || person.sector != core::Sector::rural) {
        return;
    }

    const double under = parameters_->rural_prevalence.at(kUnder18).at(person.gender);
    const double over = parameters_->rural_prevalence.at(kOver18).at(person.gender);
    if (under <= 0.0) {
        return;
    }

    const double rural_to_urban = 1.0 - over / under;
    if (random.next_double() < rural_to_urban) {
        person.sector = core::Sector::urban;
    }
}

double StaticLinearModel::continuous_income_of(const Person &person,
                                               rng::RandomSource &random) const {
    double income = evaluate_linear_model(person, parameters_->continuous_income_model,
                                          eval_options(person));

    const double stddev = coefficient_or(parameters_->continuous_income_model, kStdDev, 0.0);
    if (stddev > 0.0) {
        income += random.next_normal(0.0, stddev);
    }

    // The CSV's own min and max rows, which bound the regression rather than the risk factor.
    const double lower =
        coefficient_or(parameters_->continuous_income_model, kMin,
                       -std::numeric_limits<double>::infinity());
    const double upper =
        coefficient_or(parameters_->continuous_income_model, kMax,
                       std::numeric_limits<double>::infinity());

    return std::min(std::max(income, lower), upper);
}

void StaticLinearModel::initialise_income(RuntimeContext & /*context*/, Person &person,
                                           rng::RandomSource &random) const {
    if (!parameters_->income_enabled || !person.is_active()) {
        return;
    }

    if (parameters_->continuous_income) {
        const double income = continuous_income_of(person, random);
        person.risk_factors[kIncome] = income;
        person.income_continuous = income;
        // The category is assigned later, from the rank of the *calibrated* income, so that the
        // quintiles are quintiles of what the model finally reports rather than of the raw
        // regression output.
        person.income = core::Income::unknown;
        return;
    }

    // The categorical shape: one logit per category, softmax, then a draw from the ordered
    // distribution. The order is the layout's, which comes from the config — not a container's
    // iteration order, which is how the baseline gets it (audit B-05).
    std::vector<double> logits;
    logits.reserve(parameters_->income_layout.strata.size());
    for (const auto category : parameters_->income_layout.strata) {
        const auto model = parameters_->income_models.find(category);
        if (model == parameters_->income_models.end()) {
            throw diag::InternalError(
                fmt::format("no income model for category '{}'", core::income_name(category)));
        }
        logits.push_back(evaluate_linear_model(person, model->second, eval_options(person)));
    }

    const auto weights = softmax(logits);
    const auto distribution = rng::Categorical<core::Income>::from_weights(
        parameters_->income_layout.strata, weights);

    person.income = distribution.sample(random);
    // Stored as a number too, because the mapping and the output read income from here.
    person.risk_factors[kIncome] = static_cast<double>(person.income_to_value());
}

void StaticLinearModel::update_income(RuntimeContext &context, Person &person,
                                       rng::RandomSource &random) const {
    // Income is drawn once in childhood and once again at 18, when a person enters the labour
    // market. Every other year it is carried over.
    if (person.age == 18) {
        initialise_income(context, person, random);
    }
}

} // namespace hgps::model
