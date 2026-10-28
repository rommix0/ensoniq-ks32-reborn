#pragma once
// A fixed-size queue for one producer thread and one consumer thread: no locks, no allocation.
#include <array>
#include <atomic>
#include <cstddef>

namespace ens::app {

template <class T, size_t N>
class SpscRing {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N a power of two: the counters wrap cleanly");

public:
    bool push(const T& v) {  // producer; false when full
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t - head_.load(std::memory_order_acquire) == N) return false;
        buf_[t % N] = v;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& v) {  // consumer; false when empty
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire)) return false;
        v = buf_[h % N];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, N> buf_{};
    std::atomic<size_t> head_{0}, tail_{0};  // pops and pushes so far
};

}  // namespace ens::app
