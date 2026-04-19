#include "event_bus.h"

#include "hgps_core/utils/thread_util.h"

#include <crossguid/guid.hpp>

#include <future>
#include <memory>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

namespace hgps {

std::unique_ptr<EventSubscriber>
DefaultEventBus::subscribe(EventType event_id,
                           std::function<void(std::shared_ptr<EventMessage> message)> function) {
    std::unique_lock<mutex_type> lock(subscribe_mutex_);

    const auto event_key = static_cast<int>(event_id);
    auto handle_id = xg::newGuid().str();

    subscribers_.emplace(handle_id, std::move(function));
    registry_.emplace(event_key, handle_id);

    return std::make_unique<EventSubscriberHandler>(handle_id, this);
}

void DefaultEventBus::publish(std::unique_ptr<EventMessage> message) const {
    std::shared_ptr<EventMessage> shared_message = std::move(message);
    std::vector<std::function<void(std::shared_ptr<EventMessage>)>> handlers;

    {
        std::shared_lock<mutex_type> lock(subscribe_mutex_);

        const auto [begin_id, end_id] = registry_.equal_range(shared_message->id());
        for (auto it = begin_id; it != end_id; ++it) {
            auto handler_it = subscribers_.find(it->second);
            if (handler_it != subscribers_.end()) {
                handlers.push_back(handler_it->second);
            }
        }
    }

    for (const auto &handler : handlers) {
        handler(shared_message);
    }
}

void DefaultEventBus::publish_async(std::unique_ptr<EventMessage> message) const {
    std::ignore = core::run_async([this, msg = std::move(message)]() mutable {
        publish(std::move(msg));
    });
}

bool DefaultEventBus::unsubscribe(const EventSubscriber &subscriber) {
    std::unique_lock<mutex_type> lock(subscribe_mutex_);

    const auto sub_id = subscriber.id().str();
    if (!subscribers_.contains(sub_id)) {
        return false;
    }

    subscribers_.erase(sub_id);

    for (auto it = registry_.begin(); it != registry_.end();) {
        if (it->second == sub_id) {
            it = registry_.erase(it);
        } else {
            ++it;
        }
    }

    return true;
}

std::size_t DefaultEventBus::count() const {
    std::shared_lock<mutex_type> lock(subscribe_mutex_);
    return registry_.size();
}

void DefaultEventBus::clear() noexcept {
    std::unique_lock<mutex_type> lock(subscribe_mutex_);
    registry_.clear();
    subscribers_.clear();
}

} // namespace hgps