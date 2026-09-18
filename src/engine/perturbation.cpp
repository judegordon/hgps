#include "perturbation.h"

#include "core/string_util.h"

#include <fmt/format.h>

namespace hgps::engine {
namespace {

std::vector<std::string> split(const std::string &text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto found = text.find(separator, start);
        if (found == std::string::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, found - start));
        start = found + 1;
    }
}

std::string trim(const std::string &text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

} // namespace

std::optional<Perturbation> Perturbation::parse(const std::string &specification,
                                                std::string &error) {
    Perturbation result;
    result.specification_ = specification;

    if (trim(specification).empty()) {
        return result;
    }

    for (const auto &entry : split(specification, ';')) {
        const auto text = trim(entry);
        if (text.empty()) {
            continue;
        }

        const auto equals = text.find('=');
        if (equals == std::string::npos) {
            error = fmt::format("'{}' is not 'channel=op:value'", text);
            return std::nullopt;
        }

        PerturbationRule rule;
        rule.channel = trim(text.substr(0, equals));
        if (rule.channel.empty()) {
            error = fmt::format("'{}' names no channel", text);
            return std::nullopt;
        }

        const auto remainder = trim(text.substr(equals + 1));
        const auto colon = remainder.find(':');
        if (colon == std::string::npos) {
            error = fmt::format("'{}' is not 'channel=op:value'", text);
            return std::nullopt;
        }

        const auto operation = trim(remainder.substr(0, colon));
        if (operation == "scale") {
            rule.operation = PerturbationRule::Operation::scale;
        } else if (operation == "step") {
            rule.operation = PerturbationRule::Operation::step;
        } else {
            error = fmt::format("'{}' is not an operation; use 'scale' or 'step'", operation);
            return std::nullopt;
        }

        const auto number = trim(remainder.substr(colon + 1));
        try {
            std::size_t consumed = 0;
            rule.value = std::stod(number, &consumed);
            if (consumed != number.size()) {
                throw std::invalid_argument{"trailing characters"};
            }
        } catch (const std::exception &) {
            error = fmt::format("'{}' is not a number", number);
            return std::nullopt;
        }

        result.rules_.push_back(std::move(rule));
        result.applied_.push_back(0);
    }

    if (result.rules_.empty()) {
        error = fmt::format("'{}' names no rules", specification);
        return std::nullopt;
    }

    return result;
}

std::vector<std::string> Perturbation::rules_that_never_fired() const {
    std::vector<std::string> names;
    for (std::size_t r = 0; r < rules_.size(); ++r) {
        if (applied_[r] == 0) {
            names.push_back(rules_[r].channel);
        }
    }
    return names;
}

std::vector<std::string> Perturbation::channels() const {
    std::vector<std::string> names;
    names.reserve(rules_.size());
    for (const auto &rule : rules_) {
        names.push_back(rule.channel);
    }
    return names;
}

void Perturbation::apply(model::ModelResult &result) {
    auto &series = result.series;

    for (std::size_t r = 0; r < rules_.size(); ++r) {
        const auto &rule = rules_[r];
        if (!series.contains(rule.channel)) {
            // Counted rather than thrown: `rules_that_never_fired` reports it after the run, where
            // the caller can turn it into a diagnostic. Throwing here would abort a run half-written.
            continue;
        }
        ++applied_[r];

        for (const auto gender : {core::Gender::male, core::Gender::female}) {
            auto &values = series.at(gender, rule.channel);
            const auto &counts = series.at(gender, "count");

            switch (rule.operation) {
            case PerturbationRule::Operation::scale:
                for (auto &value : values) {
                    value *= rule.value;
                }
                break;

            case PerturbationRule::Operation::step:
                for (std::size_t index = 0; index < values.size(); ++index) {
                    if (counts.at(index) > 0.0) {
                        values.at(index) += rule.value;
                        break;
                    }
                }
                break;
            }
        }
    }
}

} // namespace hgps::engine
