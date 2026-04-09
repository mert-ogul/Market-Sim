#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <unordered_map>

#include "marketsim/domain/risk_config.hpp"
#include "marketsim/matching/types.hpp"
#include "marketsim/portfolio/portfolio.hpp"
#include "marketsim/portfolio/risk_manager.hpp"

namespace {

using marketsim::domain::OrderType;
using marketsim::domain::PriceCents;
using marketsim::domain::Quantity;
using marketsim::domain::RiskLimits;
using marketsim::domain::Side;
using marketsim::matching::SubmitRequest;
using marketsim::portfolio::Portfolio;
using marketsim::risk::RiskManager;
using marketsim::risk::RiskRejectReason;

SubmitRequest limit_buy(const std::string& sym, const PriceCents px, const Quantity qty) {
  SubmitRequest r;
  r.symbol = sym;
  r.side = Side::Buy;
  r.type = OrderType::Limit;
  r.limit_price = px;
  r.quantity = qty;
  return r;
}

TEST(Portfolio, BuyFillOpensPosition) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 100, 10));
  EXPECT_EQ(p.position_quantity("AAPL"), 10);
  EXPECT_EQ(p.avg_entry_cents("AAPL"), 100);
  EXPECT_EQ(p.cash_cents(), 1'000'000 - 1000);
}

TEST(Portfolio, AddToPositionRecalculatesAverageCost) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 100, 10));
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 200, 10));
  EXPECT_EQ(p.position_quantity("AAPL"), 20);
  EXPECT_EQ(p.avg_entry_cents("AAPL"), 150);
}

TEST(Portfolio, PartialCloseRealizesPnl) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 100, 10));
  ASSERT_TRUE(p.apply_fill(Side::Sell, "AAPL", 130, 4));
  EXPECT_EQ(p.position_quantity("AAPL"), 6);
  EXPECT_EQ(p.realized_pnl_cents("AAPL"), (130 - 100) * 4);
  EXPECT_EQ(p.avg_entry_cents("AAPL"), 100);
}

TEST(Portfolio, FullCloseResetsQuantityAndAvg) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 100, 10));
  ASSERT_TRUE(p.apply_fill(Side::Sell, "AAPL", 110, 10));
  EXPECT_EQ(p.position_quantity("AAPL"), 0);
  EXPECT_EQ(p.avg_entry_cents("AAPL"), 0);
  EXPECT_EQ(p.realized_pnl_cents("AAPL"), (110 - 100) * 10);
}

TEST(Portfolio, UnrealizedPnlUpdatesOnMark) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 100, 10));
  p.apply_mark("AAPL", 125);
  EXPECT_EQ(p.unrealized_pnl_cents("AAPL"), 10 * (125 - 100));
  EXPECT_EQ(p.market_value_cents("AAPL"), 10 * 125);
}

TEST(Portfolio, LongToShortTransition) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 100, 10));
  ASSERT_TRUE(p.apply_fill(Side::Sell, "AAPL", 110, 15));
  EXPECT_EQ(p.realized_pnl_cents("AAPL"), (110 - 100) * 10);
  EXPECT_EQ(p.position_quantity("AAPL"), -5);
  EXPECT_EQ(p.avg_entry_cents("AAPL"), 110);
}

TEST(Portfolio, ShortToLongTransition) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Sell, "AAPL", 100, 10));
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 90, 15));
  EXPECT_EQ(p.realized_pnl_cents("AAPL"), (100 - 90) * 10);
  EXPECT_EQ(p.position_quantity("AAPL"), 5);
  EXPECT_EQ(p.avg_entry_cents("AAPL"), 90);
}

TEST(Portfolio, JsonSnapshotContainsKeys) {
  Portfolio p(5000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "X", 10, 2));
  const auto j = p.to_json();
  EXPECT_TRUE(j.contains("cash_cents"));
  EXPECT_TRUE(j.contains("equity_cents"));
  EXPECT_TRUE(j.contains("positions"));
  EXPECT_TRUE(j["positions"].is_array());
}

TEST(Portfolio, OpenPositionRowsConsistentWithJsonPositions) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 100, 10));
  ASSERT_TRUE(p.apply_fill(Side::Sell, "MSFT", 200, 5));
  p.apply_mark("AAPL", 110);

  const nlohmann::json j = p.to_json();
  const auto rows = p.open_position_rows();
  ASSERT_EQ(rows.size(), j["positions"].size());

  std::unordered_map<std::string, nlohmann::json> by_symbol;
  for (const auto& row : j["positions"]) {
    by_symbol[row["symbol"].get<std::string>()] = row;
  }
  for (const auto& r : rows) {
    const auto it = by_symbol.find(r.symbol);
    ASSERT_NE(it, by_symbol.end());
    EXPECT_EQ(r.quantity, it->second["quantity"].get<Quantity>());
    EXPECT_EQ(r.avg_entry_cents, it->second["avg_entry_cents"].get<PriceCents>());
    EXPECT_EQ(r.unrealized_pnl_cents, it->second["unrealized_pnl_cents"].get<PriceCents>());
    EXPECT_EQ(r.market_value_cents, it->second["market_value_cents"].get<PriceCents>());
  }
  EXPECT_EQ(p.cash_cents(), j["cash_cents"].get<PriceCents>());
  EXPECT_EQ(p.equity_cents(), j["equity_cents"].get<PriceCents>());
  EXPECT_EQ(p.total_realized_pnl_cents(), j["total_realized_pnl_cents"].get<PriceCents>());
  EXPECT_EQ(p.total_unrealized_pnl_cents(), j["total_unrealized_pnl_cents"].get<PriceCents>());
}

TEST(RiskManager, OrderSizeLimitRejection) {
  Portfolio p(1'000'000);
  RiskLimits lim;
  lim.max_order_size = 5;
  RiskManager rm(lim, p);
  rm.set_reference("AAPL", 100);
  const auto r = rm.check(limit_buy("AAPL", 100, 10));
  ASSERT_FALSE(r.allowed);
  EXPECT_EQ(r.reason, RiskRejectReason::OrderExceedsMaxSize);
}

TEST(RiskManager, PositionLimitRejection) {
  Portfolio p(1'000'000);
  ASSERT_TRUE(p.apply_fill(Side::Buy, "AAPL", 100, 90));
  RiskLimits lim;
  lim.max_position_per_symbol = 100;
  RiskManager rm(lim, p);
  rm.set_reference("AAPL", 100);
  const auto r = rm.check(limit_buy("AAPL", 100, 20));
  ASSERT_FALSE(r.allowed);
  EXPECT_EQ(r.reason, RiskRejectReason::PositionLimitExceeded);
}

TEST(RiskManager, CashInsufficiencyRejection) {
  Portfolio p(500);
  RiskLimits lim;
  RiskManager rm(lim, p);
  rm.set_reference("AAPL", 100);
  const auto r = rm.check(limit_buy("AAPL", 100, 10));
  ASSERT_FALSE(r.allowed);
  EXPECT_EQ(r.reason, RiskRejectReason::InsufficientCash);
}

TEST(RiskManager, PriceCollarRejection) {
  Portfolio p(1'000'000);
  RiskLimits lim;
  lim.price_collar_cents = 5;
  RiskManager rm(lim, p);
  rm.set_reference("AAPL", 100);
  const auto r = rm.check(limit_buy("AAPL", 200, 1));
  ASSERT_FALSE(r.allowed);
  EXPECT_EQ(r.reason, RiskRejectReason::PriceOutsideCollar);
}

TEST(RiskManager, AbsoluteMinMaxPriceRejection) {
  Portfolio p(1'000'000);
  RiskLimits lim;
  lim.min_price_cents = 50;
  lim.max_price_cents = 150;
  RiskManager rm(lim, p);
  rm.set_reference("AAPL", 100);
  const auto low = rm.check(limit_buy("AAPL", 40, 1));
  ASSERT_FALSE(low.allowed);
  EXPECT_EQ(low.reason, RiskRejectReason::AbsoluteMinMaxPrice);
  const auto high = rm.check(limit_buy("AAPL", 200, 1));
  ASSERT_FALSE(high.allowed);
  EXPECT_EQ(high.reason, RiskRejectReason::AbsoluteMinMaxPrice);
}

TEST(RiskManager, MarketBuyWithoutReferenceRejected) {
  Portfolio p(1'000'000);
  RiskLimits lim;
  RiskManager rm(lim, p);
  SubmitRequest r;
  r.symbol = "AAPL";
  r.side = Side::Buy;
  r.type = OrderType::Market;
  r.quantity = 1;
  const auto out = rm.check(r);
  ASSERT_FALSE(out.allowed);
  EXPECT_EQ(out.reason, RiskRejectReason::UnknownSymbolReference);
}

}
