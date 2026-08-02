// bench_matching.cpp
// Measures matching latency and throughput under a synthetic order flow
// that mixes resting limit orders (no cross) with aggressive orders that
// cross the book -- the two very different cost paths a matching engine
// hits in production.

#include <chrono>
#include <cstdio>
#include <random>

#include "matching_engine.hpp"

using namespace matchengine;
using Clock = std::chrono::steady_clock;

int main() {
    constexpr int kNumOrders = 500'000;
    constexpr PriceTicks kMidPrice = 10000; // $100.00
    constexpr PriceTicks kSpread = 500;     // +/- $5.00 of book depth

    MatchingEngine engine("BENCH");
    engine.reserve_latency_capacity(kNumOrders);
    engine.start();

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<PriceTicks> offset_dist(-kSpread, kSpread);
    std::uniform_int_distribution<Quantity> qty_dist(1, 100);

    auto t0 = Clock::now();

    for (int i = 0; i < kNumOrders; ++i) {
        Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        PriceTicks offset = offset_dist(rng);
        // Bias buys toward the low side and sells toward the high side so
        // most orders rest, with occasional crosses -- a rough proxy for
        // real order flow rather than every order crossing every time.
        PriceTicks price = (side == Side::Buy) ? kMidPrice - std::abs(offset)
                                                : kMidPrice + std::abs(offset);
        Quantity qty = qty_dist(rng);

        while (!engine.submit(Order{static_cast<OrderId>(i + 1), side, price, qty, qty,
                                     static_cast<Timestamp>(i)})) {
            // backpressure: queue is full, spin briefly
        }
    }

    // Drain: wait until the engine has processed everything we submitted.
    while (engine.latency_stats().count() < static_cast<size_t>(kNumOrders)) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    auto t1 = Clock::now();
    double wall_seconds = std::chrono::duration<double>(t1 - t0).count();

    const auto& stats = engine.latency_stats();
    std::printf("Orders processed:   %d\n", kNumOrders);
    std::printf("Wall time:           %.3f s\n", wall_seconds);
    std::printf("Throughput:          %.0f orders/sec\n", kNumOrders / wall_seconds);
    std::printf("Book depth at end:   %zu bid levels, %zu ask levels, %zu open orders\n",
                engine.book().bid_levels(), engine.book().ask_levels(),
                engine.book().open_order_count());
    std::printf("\nPer-order matching latency (ns):\n");
    std::printf("  min:  %.0f\n", stats.min_ns());
    std::printf("  avg:  %.0f\n", stats.avg_ns());
    std::printf("  p50:  %.0f\n", stats.percentile_ns(50));
    std::printf("  p95:  %.0f\n", stats.percentile_ns(95));
    std::printf("  p99:  %.0f\n", stats.percentile_ns(99));
    std::printf("  max:  %.0f\n", stats.max_ns());

    engine.stop();
    return 0;
}
