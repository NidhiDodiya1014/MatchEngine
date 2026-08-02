#include <gtest/gtest.h>

#include "order_book.hpp"

using namespace matchengine;

TEST(OrderBook, RestingOrderWithNoCrossJustSits) {
    OrderBook book("TEST");
    auto trades = book.add_limit_order(Order{1, Side::Buy, 100, 10, 10, 0});
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.open_order_count(), 1u);
}

TEST(OrderBook, ExactCrossProducesOneTradeAndEmptiesBothLevels) {
    OrderBook book("TEST");
    book.add_limit_order(Order{1, Side::Sell, 100, 10, 10, 0});
    auto trades = book.add_limit_order(Order{2, Side::Buy, 100, 10, 10, 1});

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_order_id, 1u);
    EXPECT_EQ(trades[0].incoming_order_id, 2u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 10);

    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.open_order_count(), 0u);
}

TEST(OrderBook, PartialFillLeavesRemainderResting) {
    OrderBook book("TEST");
    book.add_limit_order(Order{1, Side::Sell, 100, 30, 30, 0});
    auto trades = book.add_limit_order(Order{2, Side::Buy, 100, 10, 10, 1});

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 10);

    // The resting sell should still be on the book with 20 left.
    EXPECT_EQ(book.best_ask(), 100);
    EXPECT_EQ(book.open_order_count(), 1u);
}

TEST(OrderBook, IncomingOrderCanSweepMultiplePriceLevels) {
    OrderBook book("TEST");
    book.add_limit_order(Order{1, Side::Sell, 100, 10, 10, 0});
    book.add_limit_order(Order{2, Side::Sell, 101, 10, 10, 1});
    book.add_limit_order(Order{3, Side::Sell, 102, 10, 10, 2});

    // Aggressive buy at 102 should sweep all three levels (30 total).
    auto trades = book.add_limit_order(Order{4, Side::Buy, 102, 30, 30, 3});

    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[0].price, 100); // best price fills first
    EXPECT_EQ(trades[1].price, 101);
    EXPECT_EQ(trades[2].price, 102);
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.open_order_count(), 0u);
}

TEST(OrderBook, TimePriorityAtSamePriceLevelIsFifo) {
    OrderBook book("TEST");
    book.add_limit_order(Order{1, Side::Sell, 100, 10, 10, 0}); // first in
    book.add_limit_order(Order{2, Side::Sell, 100, 10, 10, 1}); // second in

    // A buy for 10 should match the FIRST resting order (id=1), not id=2.
    auto trades = book.add_limit_order(Order{3, Side::Buy, 100, 10, 10, 2});

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_order_id, 1u);
}

TEST(OrderBook, NonCrossingOrderDoesNotMatch) {
    OrderBook book("TEST");
    book.add_limit_order(Order{1, Side::Sell, 105, 10, 10, 0});
    auto trades = book.add_limit_order(Order{2, Side::Buy, 100, 10, 10, 1});

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 105);
}

TEST(OrderBook, CancelRemovesRestingOrderAndFreesEmptyLevel) {
    OrderBook book("TEST");
    book.add_limit_order(Order{1, Side::Buy, 100, 10, 10, 0});

    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.open_order_count(), 0u);
}

TEST(OrderBook, CancelUnknownIdReturnsFalse) {
    OrderBook book("TEST");
    EXPECT_FALSE(book.cancel_order(999));
}

TEST(OrderBook, CancelOneOfTwoOrdersAtSameLevelKeepsTheOther) {
    OrderBook book("TEST");
    book.add_limit_order(Order{1, Side::Buy, 100, 10, 10, 0});
    book.add_limit_order(Order{2, Side::Buy, 100, 5, 5, 1});

    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.open_order_count(), 1u);

    // Remaining order (id=2) should still be matchable.
    auto trades = book.add_limit_order(Order{3, Side::Sell, 100, 5, 5, 2});
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_order_id, 2u);
}

TEST(OrderBook, MultipleClientsAtBestBidAndAskTracked) {
    OrderBook book("TEST");
    book.add_limit_order(Order{1, Side::Buy, 99, 10, 10, 0});
    book.add_limit_order(Order{2, Side::Buy, 100, 10, 10, 1}); // better bid
    book.add_limit_order(Order{3, Side::Sell, 105, 10, 10, 2});
    book.add_limit_order(Order{4, Side::Sell, 104, 10, 10, 3}); // better ask

    EXPECT_EQ(book.best_bid(), 100); // highest bid wins
    EXPECT_EQ(book.best_ask(), 104); // lowest ask wins
    EXPECT_EQ(book.bid_levels(), 2u);
    EXPECT_EQ(book.ask_levels(), 2u);
}
