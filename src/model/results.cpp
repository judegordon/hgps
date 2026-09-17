#include "results.h"

#include "diagnostics/internal_error.h"

#include <sstream>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {
namespace {

const char *income_name(core::Income income) {
    switch (income) {
    case core::Income::low:
        return "low";
    case core::Income::lowermiddle:
        return "lowermiddle";
    case core::Income::middle:
        return "middle";
    case core::Income::uppermiddle:
        return "uppermiddle";
    case core::Income::high:
        return "high";
    case core::Income::unknown:
        break;
    }
    return "unknown";
}

} // namespace

double &ResultByIncome::at(core::Income income) {
    switch (income) {
    case core::Income::low:
        return low;
    case core::Income::lowermiddle:
        return lowermiddle;
    case core::Income::middle:
        return middle;
    case core::Income::uppermiddle:
        return uppermiddle;
    case core::Income::high:
        return high;
    case core::Income::unknown:
        break;
    }
    throw diag::InternalError("a result cannot be recorded against an unknown income category");
}

double ResultByIncome::at(core::Income income) const {
    return const_cast<ResultByIncome *>(this)->at(income);
}

ResultByGender &ResultByIncomeGender::at(core::Income income) {
    switch (income) {
    case core::Income::low:
        return low;
    case core::Income::lowermiddle:
        return lowermiddle;
    case core::Income::middle:
        return middle;
    case core::Income::uppermiddle:
        return uppermiddle;
    case core::Income::high:
        return high;
    case core::Income::unknown:
        break;
    }
    throw diag::InternalError("a result cannot be recorded against an unknown income category");
}

const ResultByGender &ResultByIncomeGender::at(core::Income income) const {
    return const_cast<ResultByIncomeGender *>(this)->at(income);
}

DataSeries::DataSeries(std::size_t sample_size) : sample_size_{sample_size} {
    for (const auto gender : {core::Gender::male, core::Gender::female}) {
        by_gender_[gender] = {};
    }
}

void DataSeries::add_channel(std::string key) {
    if (contains(key)) {
        throw std::logic_error(fmt::format("result channel '{}' is already defined", key));
    }

    for (auto &[gender, channels] : by_gender_) {
        channels.emplace(key, std::vector<double>(sample_size_, 0.0));
    }

    channels_.push_back(std::move(key));
}

void DataSeries::add_channels(const std::vector<std::string> &keys) {
    for (const auto &key : keys) {
        add_channel(key);
    }
}

bool DataSeries::contains(const std::string &key) const noexcept {
    const auto found = by_gender_.find(core::Gender::male);
    return found != by_gender_.end() && found->second.contains(key);
}

std::vector<double> &DataSeries::at(core::Gender gender, const std::string &key) {
    return by_gender_.at(gender).at(key);
}

const std::vector<double> &DataSeries::at(core::Gender gender, const std::string &key) const {
    return by_gender_.at(gender).at(key);
}

std::vector<double> &DataSeries::at(core::Gender gender, core::Income income,
                                    const std::string &key) {
    if (!contains(key)) {
        throw std::out_of_range(fmt::format("result channel '{}' is not defined", key));
    }

    auto &for_income = by_income_[income];
    auto &for_gender = for_income[gender];
    const auto found = for_gender.find(key);
    if (found != for_gender.end()) {
        return found->second;
    }

    return for_gender.emplace(key, std::vector<double>(sample_size_, 0.0)).first->second;
}

const std::vector<double> &DataSeries::at(core::Gender gender, core::Income income,
                                          const std::string &key) const {
    return by_income_.at(income).at(gender).at(key);
}

bool RuntimeMetric::emplace(const std::string &key, double value) {
    return metrics_.emplace(key, value).second;
}

bool RuntimeMetric::erase(const std::string &key) { return metrics_.erase(key) > 0; }

void RuntimeMetric::reset() noexcept {
    for (auto &[key, value] : metrics_) {
        value = 0.0;
    }
}

ModelResult::ModelResult(std::size_t sample_size) : series{sample_size} {}

std::string ModelResult::to_string() const {
    std::stringstream stream;

    stream << fmt::format("Population size ..= {}\n", population_size);
    stream << fmt::format("Number alive .....= {} male, {} female\n", number_alive.male,
                          number_alive.female);
    stream << fmt::format("Number dead ......= {}\n", number_dead);
    stream << fmt::format("Number emigrated .= {}\n", number_emigrated);
    stream << fmt::format("Average age ......= {:.2f} male, {:.2f} female\n", average_age.male,
                          average_age.female);
    stream << fmt::format("YLL / YLD / DALY .= {:.3f} / {:.3f} / {:.3f}\n",
                          indicators.years_of_life_lost,
                          indicators.years_lived_with_disability,
                          indicators.disability_adjusted_life_years);

    for (const auto &[name, value] : risk_factor_average) {
        stream << fmt::format("  {:<24} {:>12.4f} {:>12.4f}\n", name, value.male, value.female);
    }
    for (const auto &[name, value] : disease_prevalence) {
        stream << fmt::format("  {:<24} {:>12.4f} {:>12.4f}\n", name, value.male, value.female);
    }
    if (population_by_income.has_value()) {
        for (const auto income : {core::Income::low, core::Income::lowermiddle,
                                  core::Income::middle, core::Income::uppermiddle,
                                  core::Income::high}) {
            stream << fmt::format("  income {:<17} {:>12.1f}\n", income_name(income),
                                  population_by_income->at(income));
        }
    }

    return stream.str();
}

} // namespace hgps::model
