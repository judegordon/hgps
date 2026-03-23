#pragma once

#include "poco.h"
#include "riskmodel.h"

#include <nlohmann/json.hpp>

#include <optional>

namespace hgps::input {

using json = nlohmann::json;

//--------------------------------------------------------
// Core model mappings
//--------------------------------------------------------

void to_json(json &j, const CoefficientInfo &p);
void from_json(const json &j, CoefficientInfo &p);

void to_json(json &j, const LinearModelInfo &p);
void from_json(const json &j, LinearModelInfo &p);

void to_json(json &j, const Array2Info &p);
void from_json(const json &j, Array2Info &p);

void to_json(json &j, const HierarchicalLevelInfo &p);
void from_json(const json &j, HierarchicalLevelInfo &p);

//--------------------------------------------------------
// Configuration POCO mappings
//--------------------------------------------------------

void to_json(json &j, const FileInfo &p);

void to_json(json &j, const SettingsInfo &p);
void from_json(const json &j, SettingsInfo &p);

void to_json(json &j, const SESInfo &p);
void from_json(const json &j, SESInfo &p);

void to_json(json &j, const BaselineInfo &p);

void to_json(json &j, const RiskFactorInfo &p);
void from_json(const json &j, RiskFactorInfo &p);

void to_json(json &j, const VariableInfo &p);
void from_json(const json &j, VariableInfo &p);

void to_json(json &j, const FactorDynamicEquationInfo &p);
void from_json(const json &j, FactorDynamicEquationInfo &p);

//--------------------------------------------------------
// Policy mappings
//--------------------------------------------------------

void to_json(json &j, const PolicyPeriodInfo &p);
void from_json(const json &j, PolicyPeriodInfo &p);

void to_json(json &j, const PolicyImpactInfo &p);
void from_json(const json &j, PolicyImpactInfo &p);

void to_json(json &j, const PolicyAdjustmentInfo &p);
void from_json(const json &j, PolicyAdjustmentInfo &p);

void to_json(json &j, const PolicyScenarioInfo &p);
void from_json(const json &j, PolicyScenarioInfo &p);

//--------------------------------------------------------
// Output / tracking
//--------------------------------------------------------

void to_json(json &j, const IndividualIdTrackingConfig &p);
void from_json(const json &j, IndividualIdTrackingConfig &p);

void to_json(json &j, const OutputInfo &p);
void from_json(const json &j, OutputInfo &p);

} // namespace hgps::input

//--------------------------------------------------------
// Generic helpers (core)
//--------------------------------------------------------

namespace hgps::core {

using json = nlohmann::json;

template <class T>
void to_json(json &j, const Interval<T> &interval) {
    j = json::array({interval.lower(), interval.upper()});
}

template <class T>
void from_json(const json &j, Interval<T> &interval) {
    const auto vec = j.get<std::vector<T>>();
    if (vec.size() != 2) {
        throw json::type_error::create(302, "Interval arrays must have exactly two elements", nullptr);
    }
    interval = Interval<T>{vec[0], vec[1]};
}

} // namespace hgps::core

//--------------------------------------------------------
// std::optional support
//--------------------------------------------------------

namespace std {

template <typename T>
void to_json(nlohmann::json &j, const std::optional<T> &p) {
    if (p) {
        j = *p;
    } else {
        j = nullptr;
    }
}

template <typename T>
void from_json(const nlohmann::json &j, std::optional<T> &p) {
    if (j.is_null()) {
        p = std::nullopt;
    } else {
        p = j.get<T>();
    }
}

} // namespace std