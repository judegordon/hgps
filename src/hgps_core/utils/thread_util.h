#pragma once

#include <execution>
#include <future>
#include <mutex>
#include <numeric>
#include <thread>

#include <oneapi/tbb/parallel_for_each.h>

namespace hgps::core {

template <class F, class... Ts> auto run_async(F &&action, Ts &&...params) {
    return std::async(std::launch::async, std::forward<F>(action), std::forward<Ts>(params)...);
};

template <class Index, class UnaryFunction>
auto parallel_for(Index first, Index last, UnaryFunction func) {
    auto range = std::vector<size_t>(last - first + 1);
    std::iota(range.begin(), range.end(), first);
    tbb::parallel_for_each(range.begin(), range.end(), std::move(func));
}

template <class T, class UnaryPredicate>
auto find_index_of_all(const T &data, UnaryPredicate pred) {
    auto index_mutex = std::mutex{};
    auto result = std::vector<size_t>{};
    parallel_for(size_t{0}, data.size() - 1, [&](size_t index) {
        if (pred(data[index])) {
            auto lock = std::unique_lock{index_mutex};
            result.emplace_back(index);
        }
    });

    result.shrink_to_fit();
    return result;
}

} // namespace hgps::core
