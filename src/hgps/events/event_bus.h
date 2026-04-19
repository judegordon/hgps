#pragma once

#include "event_subscriber.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace hgps {

class DefaultEventBus final : public EventAggregator {
  public:
    [[nodiscard]] std::unique_ptr<EventSubscriber>
    subscribe(EventType event_id,
              std::function<void(std::shared_ptr<EventMessage> message)> function) override;

    void publish(std::unique_ptr<EventMessage> message) const override;

    void publish_async(std::unique_ptr<EventMessage> message) const override;

    bool unsubscribe(const EventSubscriber &subscriber) override;

    [[nodiscard]] std::size_t count() const;

    void clear() noexcept;

  private:
    using mutex_type = std::shared_mutex;

    mutable mutex_type subscribe_mutex_;
    std::unordered_multimap<int, std::string> registry_;
    std::unordered_map<std::string, std::function<void(std::shared_ptr<EventMessage>)>>
        subscribers_;
};

} // namespace hgps