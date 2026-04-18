#include "settings.h"

#include <stdexcept>
#include <utility>

namespace hgps {

Settings::Settings(core::Country country, float size_fraction,
                   core::IntegerInterval age_range)
    : country_{std::move(country)},
      size_fraction_{size_fraction},
      age_range_{std::move(age_range)} {

    if (size_fraction <= 0.0f || size_fraction > 1.0f) {
        throw std::out_of_range(
            "Invalid population size fraction value, must be in range (0, 1].");
    }
}

const core::Country &Settings::country() const noexcept { return country_; }

float Settings::size_fraction() const noexcept { return size_fraction_; }

const core::IntegerInterval &Settings::age_range() const noexcept { return age_range_; }

} // namespace hgps