#pragma once

#include <algorithm>
#include <future>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include <oneapi/tbb/parallel_for_each.h>

namespace hgps::core {

template <class F, class... Ts>
auto run_async(F&& action, Ts&&... params) {
    return std::async(std::launch::async, std::forward<F>(action), std::forward<Ts>(params)...);
}

template <class Index, class UnaryFunction>
void parallel_for(Index first, Index last, UnaryFunction func) {
    if (last < first) {
        return;
    }

    auto range = std::vector<std::size_t>(static_cast<std::size_t>(last - first + 1));
    std::iota(range.begin(), range.end(), static_cast<std::size_t>(first));
    tbb::parallel_for_each(range.begin(), range.end(), std::move(func));
}

template <class T, class UnaryPredicate>
auto find_index_of_all(const T& data, UnaryPredicate pred) {
    auto result = std::vector<std::size_t>{};

    if (data.size()== 0) {
        return result;
    }

    auto index_mutex = std::mutex{};
    parallel_for(std::size_t{0}, data.size() - 1, [&](std::size_t index) {
        if (pred(data[index])) {
            auto lock = std::unique_lock{index_mutex};
            result.emplace_back(index);
        }
    });

    std::sort(result.begin(), result.end());
    result.shrink_to_fit();
    return result;
}

} // namespace hgps::core