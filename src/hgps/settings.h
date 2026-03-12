#pragma once

#include <HealthGPS.Core/interval.h>
#include <HealthGPS.Core/poco.h>

namespace hgps {

class Settings {
  public:
    Settings(core::Country country, float size_fraction, const core::IntegerInterval &age_range);

    const core::Country &country() const noexcept;

    float size_fraction() const noexcept;

    const core::IntegerInterval &age_range() const noexcept;

  private:
    core::Country country_;
    float size_fraction_{};
    core::IntegerInterval age_range_;
};
} // namespace hgps
