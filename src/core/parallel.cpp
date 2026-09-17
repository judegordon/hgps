#include "parallel.h"

#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace hgps::core::parallel {
namespace {

std::size_t g_workers = 1;

// Thread-local rather than a global counter: the guard has to be true on this thread while a
// region runs, and false on a thread that is not in one.
thread_local bool t_in_region = false;

struct RegionGuard {
    RegionGuard() { t_in_region = true; }
    ~RegionGuard() { t_in_region = false; }
    RegionGuard(const RegionGuard &) = delete;
    RegionGuard &operator=(const RegionGuard &) = delete;
    RegionGuard(RegionGuard &&) = delete;
    RegionGuard &operator=(RegionGuard &&) = delete;
};

} // namespace

void set_worker_count(std::size_t workers) {
    if (in_parallel_region()) {
        throw std::logic_error(
            "The parallel worker count cannot be changed from inside a parallel region.");
    }
    g_workers = workers == 0 ? 1 : workers;
}

WorkerCountScope::WorkerCountScope(std::size_t workers) : previous_{g_workers} {
    set_worker_count(workers);
}

WorkerCountScope::~WorkerCountScope() { g_workers = previous_; }

std::size_t worker_count() noexcept { return g_workers; }

bool in_parallel_region() noexcept { return t_in_region; }

void for_each_index(std::size_t n, const std::function<void(std::size_t)> &body) {
    if (n == 0) {
        return;
    }

    const std::size_t workers = g_workers < n ? g_workers : n;

    // One thread means no thread: the sequential path is the default and the tested one, and it
    // still sets the guard so that an RNG draw inside a "parallel" body is caught even when the
    // program is running single-threaded.
    if (workers <= 1) {
        const RegionGuard guard;
        for (std::size_t i = 0; i < n; ++i) {
            body(i);
        }
        return;
    }

    std::atomic<std::size_t> next{0};
    std::mutex failure_mutex;
    std::exception_ptr failure;
    std::vector<std::thread> pool;
    pool.reserve(workers);

    for (std::size_t w = 0; w < workers; ++w) {
        pool.emplace_back([&next, &body, &failure_mutex, &failure, n] {
            const RegionGuard guard;
            for (;;) {
                const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) {
                    return;
                }

                // A body that throws must not take the process down through a worker thread's
                // unhandled exception. The first failure is kept, the rest of the range is
                // abandoned, and the exception is rethrown on the calling thread.
                try {
                    body(i);
                } catch (...) {
                    const std::scoped_lock lock{failure_mutex};
                    if (!failure) {
                        failure = std::current_exception();
                    }
                    next.store(n, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    // The calling thread is inside the region too, so a draw made here while workers run is
    // caught as well.
    {
        const RegionGuard guard;
        for (auto &thread : pool) {
            thread.join();
        }
    }

    if (failure) {
        std::rethrow_exception(failure);
    }
}

} // namespace hgps::core::parallel
