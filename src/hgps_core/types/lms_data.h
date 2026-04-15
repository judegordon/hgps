#pragma once
#include "forward_type.h"

namespace hgps::core {

struct LmsDataRow {
    int age{};

    Gender gender{};

    double lambda{};

    double mu{};

    double sigma{};
};

} // namespace hgps::core