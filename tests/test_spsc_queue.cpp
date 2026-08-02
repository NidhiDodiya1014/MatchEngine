#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "spsc_queue.hpp"

using namespace matchengine;

TEST(SpscQueue, PushThenPopInFifoOrder) {
    SpscQueue<int> q(8);
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));

    EXPECT_EQ(*q.try_pop(), 1);
    EXPECT_EQ(*q.try_pop(), 2);
    EXPECT_EQ(*q.try_pop(), 3);
    EXPECT_FALSE(q.try_pop().has_value());
}

TEST(SpscQueue, PopOnEmptyReturnsNullopt) {
    SpscQueue<int> q(4);
    EXPECT_FALSE(q.try_pop().has_value());
}

TEST(SpscQueue, PushFailsWhenFull) {
    SpscQueue<int> q(4); // usable capacity is 3 (one slot always kept empty)
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4)); // full
}

TEST(SpscQueue, WrapsAroundCorrectlyAfterDraining) {
    SpscQueue<int> q(4);
    for (int cycle = 0; cycle < 10; ++cycle) {
        EXPECT_TRUE(q.try_push(cycle));
        EXPECT_TRUE(q.try_push(cycle * 100));
        EXPECT_EQ(*q.try_pop(), cycle);
        EXPECT_EQ(*q.try_pop(), cycle * 100);
    }
}

// Concurrent producer/consumer stress test: one real thread pushes N
// items, another real thread pops them, and we verify every item arrives
// exactly once and in order -- the actual property an SPSC queue promises.
TEST(SpscQueue, ConcurrentProducerConsumerDeliversAllItemsInOrder) {
    constexpr int kCount = 200000;
    SpscQueue<int> q(1 << 12);

    std::atomic<bool> done{false};
    std::vector<int> received;
    received.reserve(kCount);

    std::thread consumer([&] {
        int expected = 0;
        while (expected < kCount) {
            auto v = q.try_pop();
            if (v) {
                EXPECT_EQ(*v, expected);
                ++expected;
            }
        }
    });

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            while (!q.try_push(i)) {
                // spin until there's room, exactly as a real producer would
            }
        }
    });

    producer.join();
    consumer.join();
    SUCCEED(); // if we got here without the EXPECT_EQ above failing, order held
}
