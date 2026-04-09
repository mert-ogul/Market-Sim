#include <gtest/gtest.h>

#include "marketsim/domain/order.hpp"
#include "marketsim/orderbook/order_book.hpp"

namespace {

using marketsim::domain::Order;
using marketsim::domain::OrderId;
using marketsim::domain::OrderType;
using marketsim::domain::PriceCents;
using marketsim::domain::Quantity;
using marketsim::domain::Side;
using marketsim::domain::SimTime;
using marketsim::orderbook::OrderBook;

Order make_limit(const OrderId id, const std::string& sym, const Side side,
                 const PriceCents px, const Quantity qty, const SimTime t = 1) {
  Order o;
  o.id = id;
  o.symbol = sym;
  o.side = side;
  o.type = OrderType::Limit;
  o.limit_price = px;
  o.quantity = qty;
  o.remaining = qty;
  o.created_time = t;
  return o;
}

TEST(OrderBook, InsertBidAndAskSetsBestPrices) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 10)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Sell, 110, 5)));
  ASSERT_EQ(book.best_bid(), 100);
  ASSERT_EQ(book.best_ask(), 110);
  ASSERT_EQ(book.spread(), 10);
}

TEST(OrderBook, BestBidChoosesHighestPrice) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 95, 1)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Buy, 100, 1)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(3, "DEMO", Side::Buy, 98, 1)));
  ASSERT_EQ(book.best_bid(), 100);
}

TEST(OrderBook, BestAskChoosesLowestPrice) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Sell, 105, 1)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Sell, 102, 1)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(3, "DEMO", Side::Sell, 110, 1)));
  ASSERT_EQ(book.best_ask(), 102);
}

TEST(OrderBook, FifoSamePriceCancelMiddle) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 10, 1)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Buy, 100, 20, 2)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(3, "DEMO", Side::Buy, 100, 30, 3)));
  ASSERT_EQ(book.bid_volume_at_best(), 60);
  ASSERT_TRUE(book.cancel_order(2));
  ASSERT_EQ(book.bid_volume_at_best(), 40);
  ASSERT_EQ(book.order_count(), 2U);
}

TEST(OrderBook, FifoPartialFillDrainsFrontFirst) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 10, 1)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Buy, 100, 50, 2)));
  ASSERT_TRUE(book.apply_fill(1, 10));
  ASSERT_FALSE(book.cancel_order(1));
  ASSERT_EQ(book.bid_volume_at_best(), 50);
  ASSERT_TRUE(book.cancel_order(2));
  ASSERT_TRUE(book.empty());
}

TEST(OrderBook, CancelExistingAndMissing) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 10)));
  ASSERT_TRUE(book.cancel_order(1));
  ASSERT_FALSE(book.cancel_order(1));
  ASSERT_FALSE(book.cancel_order(999));
}

TEST(OrderBook, DepthSnapshotAggregatesLevels) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 10)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Buy, 99, 5)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(3, "DEMO", Side::Buy, 99, 5)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(4, "DEMO", Side::Sell, 101, 7)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(5, "DEMO", Side::Sell, 102, 3)));

  const auto bids = book.depth_bids(5);
  ASSERT_EQ(bids.size(), 2U);
  EXPECT_EQ(bids[0].price, 100);
  EXPECT_EQ(bids[0].total_quantity, 10);
  EXPECT_EQ(bids[0].order_count, 1U);
  EXPECT_EQ(bids[1].price, 99);
  EXPECT_EQ(bids[1].total_quantity, 10);
  EXPECT_EQ(bids[1].order_count, 2U);

  const auto asks = book.depth_asks(5);
  ASSERT_EQ(asks.size(), 2U);
  EXPECT_EQ(asks[0].price, 101);
  EXPECT_EQ(asks[0].total_quantity, 7);
  EXPECT_EQ(asks[1].price, 102);
  EXPECT_EQ(asks[1].total_quantity, 3);
}

TEST(OrderBook, DepthRespectsMaxLevels) {
  OrderBook book("DEMO");
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(book.insert_limit_order(
        make_limit(static_cast<OrderId>(i + 1), "DEMO", Side::Buy, 100 - i, 1)));
  }
  ASSERT_EQ(book.depth_bids(2).size(), 2U);
}

TEST(OrderBook, PartialDepletionRemovesEmptyPriceLevel) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 10)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Sell, 110, 5)));
  ASSERT_TRUE(book.apply_fill(1, 10));
  ASSERT_EQ(book.best_bid(), std::nullopt);
  ASSERT_EQ(book.best_ask(), 110);
  ASSERT_TRUE(book.apply_fill(2, 5));
  ASSERT_EQ(book.best_ask(), std::nullopt);
  ASSERT_TRUE(book.empty());
}

TEST(OrderBook, EmptyBookQueries) {
  OrderBook book("DEMO");
  EXPECT_TRUE(book.empty());
  EXPECT_EQ(book.order_count(), 0U);
  EXPECT_EQ(book.best_bid(), std::nullopt);
  EXPECT_EQ(book.best_ask(), std::nullopt);
  EXPECT_EQ(book.spread(), std::nullopt);
  EXPECT_EQ(book.bid_volume_at_best(), 0);
  EXPECT_EQ(book.ask_volume_at_best(), 0);
  EXPECT_TRUE(book.depth_bids(10).empty());
  EXPECT_TRUE(book.depth_asks(10).empty());
  const auto tob = book.top_of_book();
  EXPECT_FALSE(tob.bid_price.has_value());
  EXPECT_FALSE(tob.ask_price.has_value());
}

TEST(OrderBook, SpreadOnlyWhenBothSides) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 1)));
  ASSERT_EQ(book.spread(), std::nullopt);
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Sell, 105, 1)));
  ASSERT_EQ(book.spread(), 5);
}

TEST(OrderBook, RejectsWrongSymbolDuplicateIdInvalidLimit) {
  OrderBook book("DEMO");
  EXPECT_FALSE(book.insert_limit_order(make_limit(1, "OTHER", Side::Buy, 100, 1)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 1)));
  EXPECT_FALSE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 1)));

  Order market_style = make_limit(2, "DEMO", Side::Buy, 100, 1);
  market_style.type = OrderType::Market;
  EXPECT_FALSE(book.insert_limit_order(market_style));

  Order bad_remaining = make_limit(3, "DEMO", Side::Buy, 100, 5);
  bad_remaining.remaining = 10;
  EXPECT_FALSE(book.insert_limit_order(bad_remaining));
}

TEST(OrderBook, TopOfBookSizesMatchVolumeAtBest) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 10)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(2, "DEMO", Side::Buy, 100, 5)));
  ASSERT_TRUE(book.insert_limit_order(make_limit(3, "DEMO", Side::Sell, 110, 7)));
  const auto tob = book.top_of_book();
  ASSERT_EQ(tob.bid_price, 100);
  ASSERT_EQ(tob.bid_size, 15);
  ASSERT_EQ(tob.ask_price, 110);
  ASSERT_EQ(tob.ask_size, 7);
  EXPECT_EQ(book.bid_volume_at_best(), tob.bid_size);
  EXPECT_EQ(book.ask_volume_at_best(), tob.ask_size);
}

TEST(OrderBook, ApplyFillRejectsOverfillOrUnknown) {
  OrderBook book("DEMO");
  ASSERT_TRUE(book.insert_limit_order(make_limit(1, "DEMO", Side::Buy, 100, 10)));
  EXPECT_FALSE(book.apply_fill(1, 0));
  EXPECT_FALSE(book.apply_fill(1, 11));
  EXPECT_FALSE(book.apply_fill(99, 1));
  ASSERT_TRUE(book.apply_fill(1, 10));
  EXPECT_FALSE(book.apply_fill(1, 1));
}

}
