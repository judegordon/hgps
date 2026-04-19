#include "data_series.h"

#include "fmt/core.h"
#include "hgps_core/utils/string_util.h"

#include <array>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace hgps {
namespace {
constexpr std::array<core::Gender, 2> genders = {
    core::Gender::male,
    core::Gender::female,
};

constexpr std::array<core::Income, 6> all_income_categories = {
    core::Income::unknown,
    core::Income::low,
    core::Income::lowermiddle,
    core::Income::middle,
    core::Income::uppermiddle,
    core::Income::high,
};

constexpr std::array<core::Income, 5> standard_income_categories = {
    core::Income::low,
    core::Income::lowermiddle,
    core::Income::middle,
    core::Income::uppermiddle,
    core::Income::high,
};

void validate_unique_keys(const std::vector<std::string> &keys) {
    std::unordered_set<std::string> seen;
    seen.reserve(keys.size());

    for (const auto &key : keys) {
        const auto channel_key = core::to_lower(key);
        if (!seen.insert(channel_key).second) {
            throw std::logic_error(
                fmt::format("No duplicated channel key: {} is not allowed.", key));
        }
    }
}

std::vector<core::Income> unique_income_categories(
    const std::vector<core::Income> &income_categories) {
    std::vector<core::Income> result;
    std::unordered_set<core::Income> seen;

    result.reserve(income_categories.size());

    for (const auto income : income_categories) {
        if (!seen.contains(income)) {
            result.emplace_back(income);
            seen.insert(income);
        }
    }

    return result;
}

} // namespace

DataSeries::DataSeries(std::size_t sample_size) : sample_size_{sample_size} {
    for (auto gender : genders) {
        data_.emplace(gender, std::map<std::string, std::vector<double>>{});
        income_data_.emplace(gender,
                             std::map<core::Income, std::map<std::string, std::vector<double>>>{});

        for (auto income : all_income_categories) {
            income_data_.at(gender).emplace(income, std::map<std::string, std::vector<double>>{});
        }
    }
}

std::vector<double> &DataSeries::operator()(core::Gender gender, const std::string &key) {
    return data_.at(gender).at(core::to_lower(key));
}

std::vector<double> &DataSeries::at(core::Gender gender, const std::string &key) {
    return data_.at(gender).at(core::to_lower(key));
}

const std::vector<double> &DataSeries::at(core::Gender gender, const std::string &key) const {
    return data_.at(gender).at(core::to_lower(key));
}

std::vector<double> &DataSeries::at(core::Gender gender, core::Income income,
                                    const std::string &key) {
    return income_data_.at(gender).at(income).at(core::to_lower(key));
}

const std::vector<double> &DataSeries::at(core::Gender gender, core::Income income,
                                          const std::string &key) const {
    return income_data_.at(gender).at(income).at(core::to_lower(key));
}

const std::vector<std::string> &DataSeries::channels() const noexcept { return channels_; }

void DataSeries::add_channel(std::string key) {
    auto channel_key = core::to_lower(key);
    if (core::case_insensitive::contains(channels_, channel_key)) {
        throw std::logic_error(fmt::format("No duplicated channel key: {} is not allowed.", key));
    }

    channels_.emplace_back(channel_key);
    data_.at(core::Gender::male).emplace(channel_key, std::vector<double>(sample_size_));
    data_.at(core::Gender::female).emplace(channel_key, std::vector<double>(sample_size_));
}

void DataSeries::add_channels(const std::vector<std::string> &keys) {
    validate_unique_keys(keys);

    for (const auto &item : keys) {
        add_channel(item);
    }
}

void DataSeries::add_income_channels(const std::vector<std::string> &keys) {
    validate_unique_keys(keys);

    std::vector<double> empty_vector(sample_size_);

    for (const auto &key : keys) {
        const auto channel_key = core::to_lower(key);

        for (auto gender : genders) {
            auto &gender_income_data = income_data_.at(gender);
            for (auto income : standard_income_categories) {
                auto &income_channels = gender_income_data.at(income);
                if (income_channels.find(channel_key) == income_channels.end()) {
                    income_channels.emplace(channel_key, empty_vector);
                }
            }
        }
    }
}

void DataSeries::add_income_channels_for_categories(
    const std::vector<std::string> &keys, const std::vector<core::Income> &income_categories) {
    validate_unique_keys(keys);

    const auto unique_categories = unique_income_categories(income_categories);
    std::vector<double> empty_vector(sample_size_);

    for (const auto &key : keys) {
        const auto channel_key = core::to_lower(key);

        for (auto gender : genders) {
            auto &gender_income_data = income_data_.at(gender);
            for (auto income : unique_categories) {
                auto &income_channels = gender_income_data.at(income);
                if (income_channels.find(channel_key) == income_channels.end()) {
                    income_channels.emplace(channel_key, empty_vector);
                }
            }
        }
    }
}

std::size_t DataSeries::size() const noexcept { return channels_.size(); }

std::size_t DataSeries::sample_size() const noexcept { return sample_size_; }

std::vector<core::Income> DataSeries::get_available_income_categories() const {
    std::vector<core::Income> categories;
    std::unordered_set<core::Income> seen;

    for (const auto &[gender, income_map] : income_data_) {
        for (const auto &[income, channel_map] : income_map) {
            if (!channel_map.empty() && !seen.contains(income)) {
                categories.push_back(income);
                seen.insert(income);
            }
        }
    }

    return categories;
}

bool DataSeries::has_income_category(core::Gender gender, core::Income income) const {
    auto gender_it = income_data_.find(gender);
    if (gender_it == income_data_.end()) {
        return false;
    }

    auto income_it = gender_it->second.find(income);
    if (income_it == gender_it->second.end()) {
        return false;
    }

    return !income_it->second.empty();
}

std::string DataSeries::income_category_to_string(core::Income income) const {
    switch (income) {
    case core::Income::unknown:
        return "Unknown";
    case core::Income::low:
        return "LowIncome";
    case core::Income::lowermiddle:
        return "LowerMiddleIncome";
    case core::Income::middle:
        return "MiddleIncome";
    case core::Income::uppermiddle:
        return "UpperMiddleIncome";
    case core::Income::high:
        return "HighIncome";
    default:
        return "Unknown";
    }
}
} // namespace hgps