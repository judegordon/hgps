#include "error_message.h"

#include <fmt/format.h>
#include <utility>

namespace hgps {

ErrorEventMessage::ErrorEventMessage(std::string sender, unsigned int run, int time,
                                     std::string what)
    : EventMessage{std::move(sender), run}, model_time{time}, message{std::move(what)} {}

int ErrorEventMessage::id() const noexcept { return static_cast<int>(EventType::error); }

std::string ErrorEventMessage::to_string() const {
    return fmt::format("Source: {}, run # {}, time: {}, cause: {}",
                       source, run_number, model_time, message);
}

void ErrorEventMessage::accept(EventMessageVisitor &visitor) const { visitor.visit(*this); }

} // namespace hgps