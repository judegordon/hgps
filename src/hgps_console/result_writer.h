#pragma once

#include "HealthGPS/result_message.h"

namespace hgps {
class ResultWriter {
  public:
    virtual ~ResultWriter() = default;

    virtual void write(const hgps::ResultEventMessage &message) = 0;
};
} // namespace hgps
