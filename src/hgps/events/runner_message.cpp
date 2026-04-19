#include "runner_message.h"

#include <fmt/format.h>

#include <utility>

namespace hgps {

RunnerEventMessage::RunnerEventMessage(std::string sender, RunnerAction run_action)
    : RunnerEventMessage(std::move(sender), run_action, 0u, 0.0) {}

RunnerEventMessage::RunnerEventMessage(std::string sender, RunnerAction run_action,
                                       double elapsed)
    : RunnerEventMessage(std::move(sender), run_action, 0u, elapsed) {}

RunnerEventMessage::RunnerEventMessage(std::string sender, RunnerAction run_action,
                                       unsigned int run)
    : RunnerEventMessage(std::move(sender), run_action, run, 0.0) {}

RunnerEventMessage::RunnerEventMessage(std::string sender, RunnerAction run_action,
                                       unsigned int run, double elapsed)
    : EventMessage{std::move(sender), run}, action{run_action}, elapsed_ms{elapsed} {}

int RunnerEventMessage::id() const noexcept { return static_cast<int>(EventType::runner); }

std::string RunnerEventMessage::to_string() const {
    switch (action) {
    case RunnerAction::start:
        return fmt::format("Source: {}, experiment started ...", source);
    case RunnerAction::run_begin:
        return fmt::format("Source: {}, run # {} began ...", source, run_number);
    case RunnerAction::cancelled:
        return fmt::format("Source: {}, experiment cancelled.", source);
    case RunnerAction::run_end:
        return fmt::format("Source: {}, run # {} ended in {}ms.", source, run_number, elapsed_ms);
    case RunnerAction::finish:
        return fmt::format("Source: {}, experiment finished in {}ms.", source, elapsed_ms);
    }

    return fmt::format("Source: {}, unknown runner event.", source);
}

void RunnerEventMessage::accept(EventMessageVisitor &visitor) const { visitor.visit(*this); }

} // namespace hgps