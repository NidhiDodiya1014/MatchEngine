#pragma once

#include <cstdint>
#include <string>

namespace matchengine {

enum class Side : uint8_t { Buy, Sell };

// Prices are represented as integer ticks (e.g. price_ticks = price * 100
// for 2 decimal places) rather than double, so price comparisons and map
// keys are exact -- floating point equality/ordering is not safe for a
// matching engine where a single off-by-epsilon comparison can cross or
// fail to cross an order incorrectly.
using PriceTicks = int64_t;
using Quantity = int64_t;
using OrderId = uint64_t;
using Timestamp = uint64_t; // monotonic sequence number, used for time priority

struct Order {
    OrderId id;
    Side side;
    PriceTicks price;
    Quantity quantity;      // remaining quantity (mutated on partial fill)
    Quantity original_qty;  // original quantity, kept for reporting
    Timestamp seq;          // insertion sequence, breaks ties at same price level
};

struct Trade {
    OrderId resting_order_id;   // the order that was already on the book
    OrderId incoming_order_id;  // the order that triggered the match
    PriceTicks price;           // trade executes at the resting order's price
    Quantity quantity;
    Timestamp seq;
};

inline const char* to_string(Side side) {
    return side == Side::Buy ? "BUY" : "SELL";
}

} // namespace matchengine
