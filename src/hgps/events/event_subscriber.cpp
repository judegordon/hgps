#include "event_subscriber.h"

#include <utility>

namespace hgps {

EventSubscriberHandler::EventSubscriberHandler(EventHandlerIdentifier id, EventAggregator *hub)
    : identifier_{std::move(id)}, event_hub_{hub} {
    if (event_hub_ == nullptr) {
        throw std::invalid_argument("The event aggregator hub argument must not be null.");
    }
}

EventSubscriberHandler::~EventSubscriberHandler() noexcept {
    try {
        unsubscribe();
    } catch (...) {
    }
}

void EventSubscriberHandler::unsubscribe() const {
    if (event_hub_ != nullptr) {
        auto *hub = event_hub_;
        event_hub_ = nullptr;
        hub->unsubscribe(*this);
    }
}

EventHandlerIdentifier EventSubscriberHandler::id() const noexcept { return identifier_; }

} // namespace hgps