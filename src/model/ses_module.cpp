#include "ses_module.h"

#include "core/string_util.h"

#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace hgps::model {

SesNoiseModule::SesNoiseModule(std::string function, std::vector<double> parameters)
    : function_{std::move(function)}, parameters_{std::move(parameters)} {
    if (!core::case_insensitive::equals("normal", function_)) {
        throw std::invalid_argument(
            fmt::format("Noise generation function: {} is not supported", function_));
    }

    if (parameters_.size() != 2) {
        throw std::invalid_argument(fmt::format(
            "Number of parameters mismatch: expected 2, received {}", parameters_.size()));
    }
}

void SesNoiseModule::initialise_population(RuntimeContext &context) {
    // Slot order, serial, because it draws.
    for (auto &person : context.population()) {
        person.ses = context.random().next_normal(parameters_[0], parameters_[1]);
    }
}

void SesNoiseModule::update_population(RuntimeContext &context) {
    // Newborns only, in slot order.
    //
    // The baseline collects the newborn indices with find_index_of_all — which gathers them from
    // parallel tasks under a mutex, in nondeterministic order — and then sorts them, with a
    // comment saying the sort is needed for repeatability (audit N-8). Walking the population in
    // order needs neither the gather nor the sort.
    for (auto &person : context.population()) {
        if (person.is_active() && person.age == 0) {
            person.ses = context.random().next_normal(parameters_[0], parameters_[1]);
        }
    }
}

} // namespace hgps::model
