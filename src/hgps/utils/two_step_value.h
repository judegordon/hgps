#pragma once
#include <utility>

namespace hgps {

template <typename TYPE>
struct TwoStepValue {
    TwoStepValue() = default;

    explicit TwoStepValue(TYPE value) : value_{value}, old_value_{} {}

    TYPE value() const { return value_; }

    TYPE old_value() const { return old_value_; }

    void set_both_values(TYPE new_value) {
        value_ = new_value;
        old_value_ = new_value;
    }

    void set_value(TYPE new_value) { old_value_ = std::exchange(value_, new_value); }

    TYPE operator()() const { return value_; }

    TwoStepValue &operator=(TYPE new_value) {
        set_value(new_value);
        return *this;
    }

    TwoStepValue clone() const {
        TwoStepValue copy;
        copy.value_ = value_;
        copy.old_value_ = old_value_;
        return copy;
    }

  private:
    TYPE value_{};
    TYPE old_value_{};
};

} // namespace hgps