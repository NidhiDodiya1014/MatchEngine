#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "matching_engine.hpp"

using namespace matchengine;

namespace {
// Polls a condition until it's true or a timeout elapses. Needed because
// the engine processes requests asynchronously on its own thread.
template <typename Fn>
bool wait_until(Fn fn, std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (fn()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

TEST(MatchingEngine, SubmittedOrderRestsOnBook) {
    MatchingEngine engine("TEST");
    engine.start();

    engine.submit(Order{1, Side::Buy, 100, 10, 10, 0});
    ASSERT_TRUE(wait_until([&] { return engine.book().open_order_count() == 1; }));
    EXPECT_EQ(engine.book().best_bid(), 100);

    engine.stop();
}

TEST(MatchingEngine, CrossingOrdersProduceTradeViaCallback) {
    MatchingEngine engine("TEST");

    std::mutex mtx;
    std::vector<Trade> observed_trades;
    engine.set_trade_callback([&](const Trade& t) {
        std::lock_guard<std::mutex> lock(mtx);
        observed_trades.push_back(t);
    });

    engine.start();
    engine.submit(Order{1, Side::Sell, 100, 10, 10, 0});
    engine.submit(Order{2, Side::Buy, 100, 10, 10, 1});

    ASSERT_TRUE(wait_until([&] {
        std::lock_guard<std::mutex> lock(mtx);
        return !observed_trades.empty();
    }));

    std::lock_guard<std::mutex> lock(mtx);
    ASSERT_EQ(observed_trades.size(), 1u);
    EXPECT_EQ(observed_trades[0].resting_order_id, 1u);
    EXPECT_EQ(observed_trades[0].incoming_order_id, 2u);

    engine.stop();
}

TEST(MatchingEngine, CancelViaQueueRemovesOrder) {
    MatchingEngine engine("TEST");
    engine.start();

    engine.submit(Order{1, Side::Buy, 100, 10, 10, 0});
    ASSERT_TRUE(wait_until([&] { return engine.book().open_order_count() == 1; }));

    engine.cancel(1);
    ASSERT_TRUE(wait_until([&] { return engine.book().open_order_count() == 0; }));

    engine.stop();
}

TEST(MatchingEngine, LatencyStatsAreRecordedPerProcessedRequest) {
    MatchingEngine engine("TEST");
    engine.start();

    for (int i = 0; i < 100; ++i) {
        engine.submit(Order{static_cast<OrderId>(i + 1), Side::Buy, 100, 1, 1,
                             static_cast<Timestamp>(i)});
    }

    ASSERT_TRUE(wait_until([&] { return engine.latency_stats().count() >= 100; }));
    EXPECT_GE(engine.latency_stats().avg_ns(), 0.0);
    EXPECT_GE(engine.latency_stats().max_ns(), engine.latency_stats().min_ns());

    engine.stop();
}
