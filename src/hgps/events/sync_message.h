#pragma once

#include <utility>

namespace hgps {

class SyncMessage {
  public:
    explicit SyncMessage(int run, int time) : run_{run}, time_{time} {}

    SyncMessage(const SyncMessage &) = delete;
    SyncMessage &operator=(const SyncMessage &) = delete;
    SyncMessage(SyncMessage &&) = delete;
    SyncMessage &operator=(SyncMessage &&) = delete;

    virtual ~SyncMessage() = default;

    int run() const noexcept { return run_; }

    int time() const noexcept { return time_; }

  private:
    int run_;
    int time_;
};

template <typename PayloadType> class SyncDataMessage final : public SyncMessage {
  public:
    explicit SyncDataMessage(int run, int time, PayloadType &&data)
        : SyncMessage(run, time), data_{std::move(data)} {}

    const PayloadType &data() const noexcept { return data_; }

  private:
    PayloadType data_;
};

} // namespace hgps