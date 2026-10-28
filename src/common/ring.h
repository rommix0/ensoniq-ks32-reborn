#pragma once
// A fixed-size byte FIFO for the real-time path: no allocation. A push to a full ring drops the
// byte (the newest is lost) and counts it.
#include <array>
#include <cstddef>
#include <cstdint>

namespace ens {

template <std::size_t N>
class ByteRing {
public:
    bool push(uint8_t b) {
        if (size_ == N) {
            ++dropped_;
            return false;
        }
        buf_[(head_ + size_++) % N] = b;
        return true;
    }
    bool pop(uint8_t& b) {
        if (size_ == 0) return false;
        b = buf_[head_];
        head_ = (head_ + 1) % N;
        --size_;
        return true;
    }
    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    void clear() { head_ = size_ = 0; }  // the dropped count stays
    uint32_t dropped() const { return dropped_; }

private:
    std::array<uint8_t, N> buf_{};
    std::size_t head_ = 0, size_ = 0;
    uint32_t dropped_ = 0;
};

}  // namespace ens
