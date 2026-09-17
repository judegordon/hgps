// Derived from Health-GPS (BSD-3-Clause, Imperial College London / INRAE); see LICENSE.
// Origin: src/HealthGPS/two_step_value.h.
#pragma once

#include <utility>

namespace hgps::model {

/// @brief A value that remembers what it was, for the year-on-year models that need both.
template <typename TYPE> struct TwoStepValue {
    TwoStepValue() = default;

    explicit TwoStepValue(TYPE value) : value_{value}, old_value_{} {}

    TYPE value() const { return value_; }
    TYPE old_value() const { return old_value_; }

    /// @brief Sets both, for initialisation where there is no previous year.
    void set_both_values(TYPE new_value) {
        value_ = new_value;
        old_value_ = new_value;
    }

    /// @brief Sets the current value; the old current becomes the previous.
    void set_value(TYPE new_value) { old_value_ = std::exchange(value_, new_value); }

    TYPE operator()() const { return value_; }

    TwoStepValue &operator=(TYPE new_value) {
        set_value(new_value);
        return *this;
    }

    /// @brief A copy with both values preserved.
    TwoStepValue clone() const {
        TwoStepValue copy{old_value_};
        copy = value_; // set_value, so the previous value becomes this one's previous value
        return copy;
    }

  private:
    TYPE value_{};
    TYPE old_value_{};
};

} // namespace hgps::model
