#include <gtest/gtest.h>

#include "marketsim/matching/engine.hpp"
#include "marketsim/matching/types.hpp"

namespace {

using marketsim::domain::OrderType;
using marketsim::domain::PriceCents;
using marketsim::domain::Quantity;
using marketsim::domain::Side;
using marketsim::matching::MatchingEngine;
using marketsim::matching::SubmitRejectReason;
using marketsim::matching::SubmitRequest;

SubmitRequest limit_buy(const std::string& sym, const PriceCents px, const Quantity qty) {
  SubmitRequest r;
  r.symbol = sym;
  r.side = Side::Buy;
  r.type = OrderType::Limit;
  r.limit_price = px;
  r.quantity = qty;
  return r;
}

SubmitRequest limit_sell(const std::string& sym, const PriceCents px, const Quantity qty) {
  SubmitRequest r;
  r.symbol = sym;
  r.side = Side::Sell;
  r.type = OrderType::Limit;
  r.limit_price = px;
  r.quantity = qty;
  return r;
}

SubmitRequest market_buy(const std::string& sym, const Quantity qty) {
  SubmitRequest r;
  r.symbol = sym;
  r.side = Side::Buy;
  r.type = OrderType::Market;
  r.quantity = qty;
  return r;
}

SubmitRequest market_sell(const std::string& sym, const Quantity qty) {
  SubmitRequest r;
  r.symbol = sym;
  r.side = Side::Sell;
  r.type = OrderType::Market;
  r.quantity = qty;
  return r;
}

TEST(MatchingEngine, FullMatchConsumesRestingOrder) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 100, 10)).accepted);
  const auto r = engine.submit_order(limit_buy("AAPL", 100, 10));
  ASSERT_TRUE(r.accepted);
  ASSERT_EQ(r.resting_quantity, 0);
  ASSERT_EQ(r.trades.size(), 1U);
  EXPECT_EQ(r.trades[0].trade.quantity, 10);
  EXPECT_EQ(r.trades[0].trade.price, 100);
  ASSERT_TRUE(r.quote_after.has_value());
  EXPECT_FALSE(r.quote_after->bid_price.has_value());
  EXPECT_FALSE(r.quote_after->ask_price.has_value());
}

TEST(MatchingEngine, PartialMatchRestsRemainder) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 100, 10)).accepted);
  const auto r = engine.submit_order(limit_buy("AAPL", 100, 25));
  ASSERT_TRUE(r.accepted);
  ASSERT_EQ(r.trades.size(), 1U);
  EXPECT_EQ(r.trades[0].trade.quantity, 10);
  ASSERT_EQ(r.resting_quantity, 15);
  ASSERT_TRUE(r.quote_after.has_value());
  ASSERT_TRUE(r.quote_after->bid_price.has_value());
  EXPECT_EQ(*r.quote_after->bid_price, 100);
  EXPECT_EQ(r.quote_after->bid_size, 15);
}

TEST(MatchingEngine, MultiLevelSweep) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 100, 5)).accepted);
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 102, 5)).accepted);
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 104, 5)).accepted);
  const auto r = engine.submit_order(limit_buy("AAPL", 200, 12));
  ASSERT_TRUE(r.accepted);
  ASSERT_EQ(r.trades.size(), 3U);
  EXPECT_EQ(r.trades[0].trade.price, 100);
  EXPECT_EQ(r.trades[1].trade.price, 102);
  EXPECT_EQ(r.trades[2].trade.price, 104);
  EXPECT_EQ(r.trades[0].trade.quantity + r.trades[1].trade.quantity + r.trades[2].trade.quantity, 12);
}

TEST(MatchingEngine, FifoWithinSamePriceLevel) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 100, 3)).accepted);
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 100, 7)).accepted);
  const auto r = engine.submit_order(limit_buy("AAPL", 100, 5));
  ASSERT_TRUE(r.accepted);
  ASSERT_EQ(r.trades.size(), 2U);
  EXPECT_EQ(r.trades[0].trade.quantity, 3);
  EXPECT_EQ(r.trades[1].trade.quantity, 2);
}

TEST(MatchingEngine, UnfilledLimitPostsToBook) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_buy("AAPL", 99, 10)).accepted);
  const auto r = engine.submit_order(limit_sell("AAPL", 100, 5));
  ASSERT_TRUE(r.accepted);
  ASSERT_TRUE(r.trades.empty());
  ASSERT_EQ(r.resting_quantity, 5);
  ASSERT_TRUE(r.quote_after.has_value());
  ASSERT_TRUE(r.quote_after->ask_price.has_value());
  EXPECT_EQ(*r.quote_after->ask_price, 100);
}

TEST(MatchingEngine, MarketBuyHitsBestAsksIocRemainder) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 100, 4)).accepted);
  const auto r = engine.submit_order(market_buy("AAPL", 10));
  ASSERT_TRUE(r.accepted);
  ASSERT_EQ(r.trades.size(), 1U);
  EXPECT_EQ(r.trades[0].trade.quantity, 4);
  EXPECT_EQ(r.resting_quantity, 0);
}

TEST(MatchingEngine, MarketSellHitsBestBidsIocRemainder) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_buy("AAPL", 100, 3)).accepted);
  const auto r = engine.submit_order(market_sell("AAPL", 10));
  ASSERT_TRUE(r.accepted);
  ASSERT_EQ(r.trades.size(), 1U);
  EXPECT_EQ(r.trades[0].trade.quantity, 3);
}

TEST(MatchingEngine, MarketBuyNoLiquidityRejected) {
  MatchingEngine engine;
  const auto r = engine.submit_order(market_buy("AAPL", 5));
  ASSERT_FALSE(r.accepted);
  ASSERT_EQ(r.reject, SubmitRejectReason::MarketNoLiquidity);
}

TEST(MatchingEngine, CancelBeforeAnotherMatch) {
  MatchingEngine engine;
  const auto a = engine.submit_order(limit_sell("AAPL", 100, 10));
  ASSERT_TRUE(a.accepted);
  const auto oid = a.order_id;
  ASSERT_TRUE(engine.cancel_order(oid).cancelled);
  const auto r = engine.submit_order(limit_buy("AAPL", 100, 10));
  ASSERT_TRUE(r.trades.empty());
  ASSERT_EQ(r.resting_quantity, 10);
}

TEST(MatchingEngine, PerSymbolIsolation) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 100, 5)).accepted);
  ASSERT_TRUE(engine.submit_order(limit_sell("MSFT", 200, 5)).accepted);
  const auto r = engine.submit_order(limit_buy("AAPL", 100, 5));
  ASSERT_TRUE(r.accepted);
  ASSERT_EQ(r.trades.size(), 1U);
  const auto q = engine.submit_order(limit_buy("MSFT", 200, 1));
  ASSERT_TRUE(q.accepted);
  ASSERT_EQ(q.trades.size(), 1U);
}

TEST(MatchingEngine, QuoteUpdatesAfterSubmit) {
  MatchingEngine engine;
  ASSERT_TRUE(engine.submit_order(limit_sell("AAPL", 100, 10)).accepted);
  const auto r = engine.submit_order(limit_buy("AAPL", 99, 5));
  ASSERT_TRUE(r.accepted);
  ASSERT_TRUE(r.quote_after.has_value());
  const auto& q = *r.quote_after;
  ASSERT_TRUE(q.bid_price.has_value());
  ASSERT_TRUE(q.ask_price.has_value());
  EXPECT_EQ(*q.bid_price, 99);
  EXPECT_EQ(*q.ask_price, 100);
}

TEST(MatchingEngine, CancelUnknownOrderNoOp) {
  MatchingEngine engine;
  EXPECT_FALSE(engine.cancel_order(42).cancelled);
}

TEST(MatchingEngine, RejectsInvalidRequests) {
  MatchingEngine engine;
  SubmitRequest bad_qty = limit_buy("AAPL", 100, 1);
  bad_qty.quantity = 0;
  EXPECT_EQ(engine.submit_order(bad_qty).reject, SubmitRejectReason::InvalidQuantity);

  SubmitRequest bad_limit = limit_buy("AAPL", 100, 1);
  bad_limit.limit_price.reset();
  EXPECT_EQ(engine.submit_order(bad_limit).reject, SubmitRejectReason::InvalidLimitPrice);

  SubmitRequest bad_mkt = market_buy("AAPL", 1);
  bad_mkt.limit_price = PriceCents{1};
  EXPECT_EQ(engine.submit_order(bad_mkt).reject, SubmitRejectReason::InvalidLimitPrice);

  EXPECT_EQ(engine.submit_order(limit_sell("AAPL", 0, 1)).reject, SubmitRejectReason::InvalidLimitPrice);
}

}
