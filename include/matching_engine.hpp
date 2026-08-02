#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <variant>

#include "latency_stats.hpp"
#include "order.hpp"
#include "order_book.hpp"
#include "spsc_queue.hpp"

namespace matchengine {

struct AddOrderRequest {
    Order order;
};

struct CancelOrderRequest {
    OrderId id;
};

using EngineRequest = std::variant<AddOrderRequest, CancelOrderRequest>;

// Runs one OrderBook on a single dedicated thread. Producers (order entry,
// simulated clients, etc.) call submit()/cancel() from any thread; those
// calls only push into a lock-free SPSC queue and return immediately. The
// engine thread drains the queue and is the *only* thread that ever calls
// into OrderBook, so the matching logic itself never needs synchronization.
//
// This mirrors how real trading venues isolate the matching engine on one
// core: predictable single-threaded latency for the hot path, with
// ingestion/networking kept off of it entirely.
class MatchingEngine {
public:
    using TradeCallback = std::function<void(const Trade&)>;

    explicit MatchingEngine(std::string symbol, size_t queue_capacity_pow2 = 1 << 16)
        : book_(std::move(symbol)), queue_(queue_capacity_pow2) {}

    ~MatchingEngine() { stop(); }

    void set_trade_callback(TradeCallback cb) { on_trade_ = std::move(cb); }

    void start() {
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this] { run(); });
    }

    void stop() {
        if (running_.exchange(false, std::memory_order_acq_rel)) {
            if (worker_.joinable()) worker_.join();
        }
    }

    // Producer-side calls. Return false if the ingestion queue is full
    // (backpressure signal -- callers should slow down, not block).
    bool submit(Order order) { return queue_.try_push(AddOrderRequest{order}); }
    bool cancel(OrderId id) { return queue_.try_push(CancelOrderRequest{id}); }

    const LatencyStats& latency_stats() const { return match_latency_; }
    void reserve_latency_capacity(size_t n) { match_latency_.reserve(n); }
    const OrderBook& book() const { return book_; }

private:
    void run() {
        while (running_.load(std::memory_order_acquire)) {
            auto req = queue_.try_pop();
            if (!req) continue; // busy-poll: real engines pin this thread to a core

            auto start = std::chrono::steady_clock::now();

            if (auto* add = std::get_if<AddOrderRequest>(&*req)) {
                auto trades = book_.add_limit_order(add->order);
                if (on_trade_) {
                    for (const auto& t : trades) on_trade_(t);
                }
            } else if (auto* c = std::get_if<CancelOrderRequest>(&*req)) {
                book_.cancel_order(c->id);
            }

            auto end = std::chrono::steady_clock::now();
            match_latency_.record(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        }
    }

    OrderBook book_;
    SpscQueue<EngineRequest> queue_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    LatencyStats match_latency_;
    TradeCallback on_trade_;
};

} // namespace matchengine
