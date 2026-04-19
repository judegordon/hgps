// event_monitor.cpp
#include "event_monitor.h"

#include "hgps/events/error_message.h"
#include "hgps/events/individual_tracking_message.h"
#include "hgps/events/info_message.h"
#include "hgps/events/runner_message.h"

#include <chrono>
#include <thread>

#include <fmt/color.h>
#include <fmt/core.h>

namespace hgps {

EventMonitor::EventMonitor(hgps::EventAggregator &event_bus, ResultWriter &result_writer,
                           IndividualIDTrackingWriter *individual_tracking_writer)
    : result_writer_{result_writer}, individual_tracking_writer_{individual_tracking_writer} {
    handlers_.emplace_back(event_bus.subscribe(hgps::EventType::runner, [this](auto &&message) {
        info_event_handler(std::forward<decltype(message)>(message));
    }));

    handlers_.emplace_back(event_bus.subscribe(hgps::EventType::info, [this](auto &&message) {
        info_event_handler(std::forward<decltype(message)>(message));
    }));

    handlers_.emplace_back(event_bus.subscribe(hgps::EventType::result, [this](auto &&message) {
        result_event_handler(std::forward<decltype(message)>(message));
    }));

    handlers_.emplace_back(event_bus.subscribe(hgps::EventType::error, [this](auto &&message) {
        error_event_handler(std::forward<decltype(message)>(message));
    }));

    if (individual_tracking_writer_ != nullptr) {
        handlers_.emplace_back(
            event_bus.subscribe(hgps::EventType::individual_tracking, [this](auto &&message) {
                tracking_event_handler(std::forward<decltype(message)>(message));
            }));
    }

    tg_.run([this] { info_dispatch_thread(); });
    tg_.run([this] { result_dispatch_thread(); });

    if (individual_tracking_writer_ != nullptr) {
        tg_.run([this] { tracking_dispatch_thread(); });
    }
}

EventMonitor::~EventMonitor() noexcept {
    stop();
}

void EventMonitor::stop() noexcept {
    const bool already_stopping = stopping_.exchange(true);
    if (already_stopping) {
        return;
    }

    for (auto &handler : handlers_) {
        handler->unsubscribe();
    }

    tg_.wait();
}

void EventMonitor::visit(const hgps::RunnerEventMessage &message) {
    fmt::print(fg(fmt::color::cornflower_blue), "{}\n", message.to_string());
}

void EventMonitor::visit(const hgps::InfoEventMessage &message) {
    fmt::print(fg(fmt::color::light_blue), "{}\n", message.to_string());
}

void EventMonitor::visit(const hgps::ErrorEventMessage &message) {
    fmt::print(fg(fmt::color::red), "{}\n", message.to_string());
}

void EventMonitor::visit(const hgps::ResultEventMessage &message) {
    result_writer_.write(message);
}

void EventMonitor::visit(const hgps::IndividualTrackingEventMessage &message) {
    if (individual_tracking_writer_ != nullptr) {
        individual_tracking_writer_->write(message);
    }
}

void EventMonitor::info_event_handler(std::shared_ptr<hgps::EventMessage> message) {
    info_queue_.emplace(std::move(message));
}

void EventMonitor::error_event_handler(const std::shared_ptr<hgps::EventMessage> &message) {
    message->accept(*this);
}

void EventMonitor::result_event_handler(std::shared_ptr<hgps::EventMessage> message) {
    results_queue_.emplace(std::move(message));
}

void EventMonitor::tracking_event_handler(std::shared_ptr<hgps::EventMessage> message) {
    tracking_results_queue_.emplace(std::move(message));
}

bool EventMonitor::should_exit(
    const tbb::concurrent_queue<std::shared_ptr<hgps::EventMessage>> &queue) const {
    return stopping_.load() && queue.empty();
}

void EventMonitor::info_dispatch_thread() {
    while (!should_exit(info_queue_)) {
        std::shared_ptr<hgps::EventMessage> message;
        if (info_queue_.try_pop(message)) {
            message->accept(*this);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void EventMonitor::result_dispatch_thread() {
    while (!should_exit(results_queue_)) {
        std::shared_ptr<hgps::EventMessage> message;
        if (results_queue_.try_pop(message)) {
            message->accept(*this);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void EventMonitor::tracking_dispatch_thread() {
    while (!should_exit(tracking_results_queue_)) {
        std::shared_ptr<hgps::EventMessage> message;
        if (tracking_results_queue_.try_pop(message)) {
            message->accept(*this);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

} // namespace hgps