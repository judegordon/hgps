#pragma once

#include "event_message.h"

#include <map>
#include <string>
#include <vector>

namespace hgps {

struct IndividualTrackingRow {
    std::size_t id{};
    unsigned int age{};
    std::string gender{};
    std::string region{};
    std::string ethnicity{};
    std::string income_category{};
    std::map<std::string, double> risk_factors{};
};

struct IndividualTrackingEventMessage final : public EventMessage {
    IndividualTrackingEventMessage() = delete;

    IndividualTrackingEventMessage(std::string sender, unsigned int run, int time,
                                   std::string scenario_name,
                                   std::vector<IndividualTrackingRow> rows);

    int model_time{};
    std::string scenario_name{};
    std::vector<IndividualTrackingRow> rows{};

    int id() const noexcept override;
    std::string to_string() const override;
    void accept(EventMessageVisitor &visitor) const override;
};

} // namespace hgps
