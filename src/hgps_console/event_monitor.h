#pragma once
#include <thread>

#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/task_group.h>

#include "hgps/event_aggregator.h"

#include "individual_id_tracking_writer.h"
#include "result_writer.h"

namespace hgps {
class EventMonitor final : public hgps::EventMessageVisitor {
  public:
    EventMonitor() = delete;

    EventMonitor(hgps::EventAggregator &event_bus, ResultWriter &result_writer,
                 IndividualIDTrackingWriter *individual_tracking_writer = nullptr);

    ~EventMonitor() noexcept;

    void stop() noexcept;

    void visit(const hgps::RunnerEventMessage &message) override;
    void visit(const hgps::InfoEventMessage &message) override;
    void visit(const hgps::ErrorEventMessage &message) override;
    void visit(const hgps::ResultEventMessage &message) override;
    void visit(const hgps::IndividualTrackingEventMessage &message) override;

  private:
    ResultWriter &result_writer_;
    IndividualIDTrackingWriter *individual_tracking_writer_{nullptr};
    tbb::task_group_context tg_context_;
    tbb::task_group tg_;
    std::vector<std::unique_ptr<hgps::EventSubscriber>> handlers_;
    tbb::concurrent_queue<std::shared_ptr<hgps::EventMessage>> info_queue_;
    tbb::concurrent_queue<std::shared_ptr<hgps::EventMessage>> results_queue_;
    // MAHIMA: Second queue so main result writes and individual-tracking writes run on separate
    // threads and can proceed in parallel (different files, no shared state).
    tbb::concurrent_queue<std::shared_ptr<hgps::EventMessage>> tracking_results_queue_;

    void info_event_handler(std::shared_ptr<hgps::EventMessage> message);
    void error_event_handler(const std::shared_ptr<hgps::EventMessage> &message);
    void result_event_handler(std::shared_ptr<hgps::EventMessage> message);
    // MAHIMA: Routes individual_tracking events to tracking_results_queue_ (not results_queue_).
    void tracking_event_handler(std::shared_ptr<hgps::EventMessage> message);

    void info_dispatch_thread();
    void result_dispatch_thread();
    // MAHIMA: Dedicated dispatch thread for tracking CSV; runs in parallel with
    // result_dispatch_thread.
    void tracking_dispatch_thread();
};
} // namespace hgps
