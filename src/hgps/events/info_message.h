#pragma once
#include "event_message.h"

#include <string>

namespace hgps {

enum class ModelAction {
    start,
    update,
    stop
};

struct InfoEventMessage final : public EventMessage {
    InfoEventMessage() = delete;

    InfoEventMessage(std::string sender, ModelAction action, unsigned int run, int time);

    InfoEventMessage(std::string sender, ModelAction action, unsigned int run, int time,
                     std::string msg);

    const ModelAction model_action{};
    const int model_time{};
    const std::string message;

    int id() const noexcept override;

    std::string to_string() const override;

    void accept(EventMessageVisitor &visitor) const override;
};

namespace detail {
std::string model_action_str(ModelAction action);
} // namespace detail

} // namespace hgps