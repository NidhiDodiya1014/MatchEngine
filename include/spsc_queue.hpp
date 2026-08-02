#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace matchengine {

// A bounded single-producer, single-consumer ring buffer.
//
// Why this exists: a real matching engine keeps the book on a single
// thread so the matching logic itself never needs a lock. Producer
// thread(s) (order entry / network I/O) hand work to that thread through
// a queue instead of calling into the book directly. Using a mutex+deque
// for that handoff would reintroduce exactly the contention this design
// is trying to avoid, so this queue uses only atomics with acquire/release
// ordering -- push and pop never block and never take a lock.
//
// Capacity must be a power of two so the index wrap-around can use a
// bitmask instead of a modulo.
template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(size_t capacity_pow2)
        : capacity_(capacity_pow2), mask_(capacity_pow2 - 1), buffer_(capacity_pow2) {
        // capacity must be a power of two for the mask trick to be valid.
    }

    // Producer side. Returns false if the queue is full.
    bool try_push(T value) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[head] = std::move(value);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns nullopt if the queue is empty.
    std::optional<T> try_pop() {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt; // empty
        }
        T value = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return value;
    }

    size_t capacity() const { return capacity_; }

private:
    size_t capacity_;
    size_t mask_;
    std::vector<T> buffer_;
    alignas(64) std::atomic<size_t> head_{0}; // producer-owned
    alignas(64) std::atomic<size_t> tail_{0}; // consumer-owned
};

} // namespace matchengine
