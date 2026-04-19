#include "info_message.h"

#include <fmt/format.h>

#include <utility>

namespace hgps {

InfoEventMessage::InfoEventMessage(std::string sender, ModelAction action, unsigned int run,
                                   int time)
    : InfoEventMessage(std::move(sender), action, run, time, std::string{}) {}

InfoEventMessage::InfoEventMessage(std::string sender, ModelAction action, unsigned int run,
                                   int time, std::string msg)
    : EventMessage{std::move(sender), run},
      model_action{action},
      model_time{time},
      message{std::move(msg)} {}

int InfoEventMessage::id() const noexcept { return static_cast<int>(EventType::info); }

std::string InfoEventMessage::to_string() const {
    if (message.empty()) {
        return fmt::format("Source: {}, run # {}, {}, time: {}",
                           source, run_number,
                           detail::model_action_str(model_action), model_time);
    }

    return fmt::format("Source: {}, run # {}, {}, time: {} - {}",
                       source, run_number,
                       detail::model_action_str(model_action), model_time, message);
}

void InfoEventMessage::accept(EventMessageVisitor &visitor) const { visitor.visit(*this); }

namespace detail {

std::string model_action_str(ModelAction action) {
    switch (action) {
    case ModelAction::update:
        return "update";
    case ModelAction::start:
        return "start";
    case ModelAction::stop:
        return "stop";
    default:
        return "unknown";
    }
}

} // namespace detail
} // namespace hgps