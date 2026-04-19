#pragma once

#include "event_visitor.h"

#include <cstdint>
#include <string>
#include <utility>

namespace hgps {

enum struct EventType : uint8_t {
    runner,
    info,
    result,
    individual_tracking,
    error
};

struct EventMessage {
    EventMessage() = delete;

    explicit EventMessage(std::string sender, unsigned int run)
        : source{std::move(sender)}, run_number{run} {}

    EventMessage(const EventMessage &) = delete;
    EventMessage(EventMessage &&) = delete;
    EventMessage &operator=(const EventMessage &) = delete;
    EventMessage &operator=(EventMessage &&) = delete;

    virtual ~EventMessage() = default;

    const std::string source;
    const unsigned int run_number{};

    virtual int id() const noexcept = 0;

    virtual std::string to_string() const = 0;

    virtual void accept(EventMessageVisitor &visitor) const = 0;
};

} // namespace hgps