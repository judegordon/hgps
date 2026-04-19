#pragma once
#include "event_message.h"

#include <string>

namespace hgps {

enum class RunnerAction {
    start,
    run_begin,
    cancelled,
    run_end,
    finish
};

struct RunnerEventMessage final : public EventMessage {
    RunnerEventMessage() = delete;

    RunnerEventMessage(std::string sender, RunnerAction run_action);

    RunnerEventMessage(std::string sender, RunnerAction run_action, double elapsed);

    RunnerEventMessage(std::string sender, RunnerAction run_action, unsigned int run);

    RunnerEventMessage(std::string sender, RunnerAction run_action, unsigned int run,
                       double elapsed);

    const RunnerAction action{};
    const double elapsed_ms{};

    int id() const noexcept override;

    std::string to_string() const override;

    void accept(EventMessageVisitor &visitor) const override;
};

} // namespace hgps