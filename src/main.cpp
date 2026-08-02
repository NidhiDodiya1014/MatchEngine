#include <chrono>
#include <iostream>
#include <thread>

#include "matching_engine.hpp"

using namespace matchengine;

int main() {
    MatchingEngine engine("DEMO");

    engine.set_trade_callback([](const Trade& t) {
        std::cout << "TRADE  resting_id=" << t.resting_order_id
                  << " incoming_id=" << t.incoming_order_id
                  << " price=" << t.price / 100.0
                  << " qty=" << t.quantity << "\n";
    });

    engine.start();

    // A small resting book: bids below, asks above.
    engine.submit(Order{1, Side::Buy, 9950, 100, 100, 0});   // $99.50 x100
    engine.submit(Order{2, Side::Buy, 9945, 200, 200, 1});   // $99.45 x200
    engine.submit(Order{3, Side::Sell, 10005, 150, 150, 2}); // $100.05 x150
    engine.submit(Order{4, Side::Sell, 10010, 300, 300, 3}); // $100.10 x300

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "-- resting book built, best_bid=" << *engine.book().best_bid() / 100.0
              << " best_ask=" << *engine.book().best_ask() / 100.0 << " --\n";

    // An aggressive buy that crosses the ask side and partially fills.
    engine.submit(Order{5, Side::Buy, 10005, 200, 200, 4});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Cancel one of the resting bids.
    engine.cancel(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "-- after cancel, bid levels=" << engine.book().bid_levels()
              << " ask levels=" << engine.book().ask_levels()
              << " open orders=" << engine.book().open_order_count() << " --\n";

    const auto& stats = engine.latency_stats();
    std::cout << "matching latency (ns): min=" << stats.min_ns()
              << " avg=" << stats.avg_ns()
              << " p99=" << stats.percentile_ns(99)
              << " max=" << stats.max_ns() << "\n";

    engine.stop();
    return 0;
}
