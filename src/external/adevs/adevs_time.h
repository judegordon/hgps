#ifndef _adevs_time_h_
#define _adevs_time_h_
#include <limits>


namespace adevs {

template <typename RealType> class SuperDenseTime {
  public:
    RealType real{};
    int logical{};
    SuperDenseTime() {}
    SuperDenseTime(RealType real, int logical) : real(real), logical(logical) {}
    SuperDenseTime<RealType> operator+(const SuperDenseTime<RealType> &h) const {
        return SuperDenseTime<RealType>(real + h.real,
                                        logical * (h.real == RealType(0)) + h.logical);
    }
    SuperDenseTime<RealType> operator-(const SuperDenseTime<RealType> &preceding) const {
        if (real == preceding.real)
            return SuperDenseTime<RealType>(RealType(0), logical - preceding.logical);
        return SuperDenseTime<RealType>(real - preceding.real, logical);
    }
    bool operator<(const SuperDenseTime<RealType> &other) const {
        return (real < other.real) || ((real == other.real) && (logical < other.logical));
    }
    bool operator<=(const SuperDenseTime<RealType> &other) const {
        return (real < other.real) || ((real == other.real) && (logical <= other.logical));
    }
    bool operator==(const SuperDenseTime<RealType> &other) const {
        return (real == other.real && logical == other.logical);
    }
};

typedef SuperDenseTime<int> Time;

}; // namespace adevs

template <class T> inline T adevs_inf();
template <> inline adevs::SuperDenseTime<int> adevs_inf() {
    return adevs::SuperDenseTime<int>(std::numeric_limits<int>::max(),
                                      std::numeric_limits<int>::max());
}
template <class T> inline T adevs_inf();
template <> inline adevs::SuperDenseTime<long> adevs_inf() {
    return adevs::SuperDenseTime<long>(std::numeric_limits<long>::max(),
                                       std::numeric_limits<int>::max());
}
template <> inline adevs::SuperDenseTime<double> adevs_inf() {
    return adevs::SuperDenseTime<double>(std::numeric_limits<double>::infinity(),
                                         std::numeric_limits<int>::max());
}
template <> inline int adevs_inf() { return std::numeric_limits<int>::max(); }

template <typename T> inline T adevs_zero();
template <> inline adevs::SuperDenseTime<int> adevs_zero() {
    return adevs::SuperDenseTime<int>(0, 0);
}
template <> inline adevs::SuperDenseTime<long> adevs_zero() {
    return adevs::SuperDenseTime<long>(0, 0);
}
template <> inline adevs::SuperDenseTime<double> adevs_zero() {
    return adevs::SuperDenseTime<double>(0.0, 0);
}
template <> inline int adevs_zero() { return 0; }

template <class T> inline T adevs_epsilon();
template <> inline adevs::SuperDenseTime<int> adevs_epsilon() {
    return adevs::SuperDenseTime<int>(0, 1);
}
template <> inline adevs::SuperDenseTime<long> adevs_epsilon() {
    return adevs::SuperDenseTime<long>(0, 1);
}
template <> inline adevs::SuperDenseTime<double> adevs_epsilon() {
    return adevs::SuperDenseTime<double>(0.0, 1);
}
template <> inline int adevs_epsilon() { return 1; }

#endif
