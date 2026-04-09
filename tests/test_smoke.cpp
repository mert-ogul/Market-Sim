#include <gtest/gtest.h>

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/order.hpp"
#include "marketsim/domain/position.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/domain/trade.hpp"
#include "marketsim/domain/types.hpp"

namespace {

TEST(DomainSmoke, TypesAndAggregatesAreUsable) {
  namespace d = marketsim::domain;

  EXPECT_EQ(d::Side::Buy, d::Side::Buy);
  EXPECT_NE(d::Side::Buy, d::Side::Sell);

  d::Order o;
  o.id = 1;
  o.symbol = "DEMO";
  o.side = d::Side::Sell;
  o.type = d::OrderType::Limit;
  o.limit_price = d::PriceCents{100};
  o.quantity = 10;
  o.remaining = 10;
  o.created_time = 42;
  EXPECT_EQ(o.symbol, "DEMO");
  EXPECT_EQ(*o.limit_price, 100);

  d::Trade t;
  t.price = d::PriceCents{100};
  t.quantity = 5;
  EXPECT_EQ(t.quantity, 5);

  d::Quote q;
  q.bid_price = d::PriceCents{99};
  q.ask_price = d::PriceCents{101};
  ASSERT_TRUE(q.bid_price.has_value());
  ASSERT_TRUE(q.ask_price.has_value());
  EXPECT_LT(*q.bid_price, *q.ask_price);

  d::Tick tick;
  tick.last_price = 100;
  tick.cumulative_volume = 1000;

  d::Position p;
  p.symbol = "DEMO";
  p.quantity = 3;
  p.average_cost_cents = 100;

  d::SimulationAccount acct;
  acct.cash_cents = 1'000'000;
  acct.limits.max_order_size = 500;
}

}
