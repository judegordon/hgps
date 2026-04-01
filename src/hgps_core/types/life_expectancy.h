#pragma once

namespace hgps::core {

struct LifeExpectancyItem {
    int at_time{};

    float both{};

    float male{};

    float female{};
};

} // namespace hgps::core