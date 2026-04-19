#include "result_message.h"
#include "event_visitor.h"

#include <fmt/format.h>
#include <utility>

namespace hgps {

ResultEventMessage::ResultEventMessage(std::string sender, unsigned int run, int time,
                                       ModelResult result)
    : EventMessage(std::move(sender), run), model_time{time}, content{std::move(result)} {}

int ResultEventMessage::id() const noexcept { return static_cast<int>(EventType::result); }

std::string ResultEventMessage::to_string() const {
    return fmt::format("[source: {}, run: {}, time: {}] model result published",
                       source, run_number, model_time);
}

void ResultEventMessage::accept(EventMessageVisitor &visitor) const { visitor.visit(*this); }

} // namespace hgps