#include "order_book.hpp"

namespace matchengine {

template <typename Book, typename ShouldMatch>
void OrderBook::match_against(Order& incoming, Book& opposite_book,
                               ShouldMatch should_match,
                               std::vector<Trade>& trades) {
    auto level_it = opposite_book.begin();
    while (incoming.quantity > 0 && level_it != opposite_book.end() &&
           should_match(level_it->first)) {
        std::list<Order>& resting_orders = level_it->second;
        auto order_it = resting_orders.begin();

        while (incoming.quantity > 0 && order_it != resting_orders.end()) {
            Order& resting = *order_it;
            Quantity fill_qty = std::min(incoming.quantity, resting.quantity);

            trades.push_back(Trade{
                resting.id, incoming.id, resting.price, fill_qty, incoming.seq});

            incoming.quantity -= fill_qty;
            resting.quantity -= fill_qty;

            if (resting.quantity == 0) {
                index_.erase(resting.id);
                order_it = resting_orders.erase(order_it);
            } else {
                ++order_it;
            }
        }

        if (resting_orders.empty()) {
            level_it = opposite_book.erase(level_it);
        } else {
            ++level_it;
        }
    }
}

std::vector<Trade> OrderBook::add_limit_order(Order order) {
    std::vector<Trade> trades;

    if (order.side == Side::Buy) {
        // A resting ask crosses a buy if ask.price <= buy.price.
        match_against(order, asks_,
                       [&](PriceTicks ask_price) { return ask_price <= order.price; },
                       trades);
    } else {
        // A resting bid crosses a sell if bid.price >= sell.price.
        match_against(order, bids_,
                       [&](PriceTicks bid_price) { return bid_price >= order.price; },
                       trades);
    }

    // Any remaining quantity rests on the book at its own price level.
    // bids_ and asks_ are different map types (different comparators), so
    // this can't be a single ternary -- each branch inserts into its own
    // container type.
    if (order.quantity > 0) {
        if (order.side == Side::Buy) {
            auto& level = bids_[order.price];
            level.push_back(order);
            index_[order.id] = Location{order.side, order.price, std::prev(level.end())};
        } else {
            auto& level = asks_[order.price];
            level.push_back(order);
            index_[order.id] = Location{order.side, order.price, std::prev(level.end())};
        }
    }

    return trades;
}

bool OrderBook::cancel_order(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;

    const Location& loc = found->second;
    if (loc.side == Side::Buy) {
        auto level_it = bids_.find(loc.price);
        level_it->second.erase(loc.it);
        if (level_it->second.empty()) bids_.erase(level_it);
    } else {
        auto level_it = asks_.find(loc.price);
        level_it->second.erase(loc.it);
        if (level_it->second.empty()) asks_.erase(level_it);
    }

    index_.erase(found);
    return true;
}

std::optional<PriceTicks> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<PriceTicks> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

} // namespace matchengine
