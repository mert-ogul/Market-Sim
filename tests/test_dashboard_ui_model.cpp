#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/gateway/simulator_service.hpp"
#include "marketsim/matching/types.hpp"
#include "marketsim/ui/dashboard_format.hpp"
#include "marketsim/ui/dashboard_prepare.hpp"
#include "marketsim/view/simulator_snapshot.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(DashboardFormat, FormatsUsdCents) {
  EXPECT_EQ(marketsim::ui::format_usd_cents(0), "$0.00");
  EXPECT_EQ(marketsim::ui::format_usd_cents(1), "$0.01");
  EXPECT_EQ(marketsim::ui::format_usd_cents(-1'25), "-$1.25");
  EXPECT_EQ(marketsim::ui::format_usd_cents(18'000), "$180.00");
}

TEST(DashboardFormat, SignedPnlUsesParenthesesForNegative) {
  EXPECT_EQ(marketsim::ui::format_signed_usd_cents(100), "$1.00");
  EXPECT_EQ(marketsim::ui::format_signed_usd_cents(-50), "($0.50)");
}

TEST(DashboardFormat, SparklineEmptyOrDegenerate) {
  EXPECT_TRUE(marketsim::ui::build_sparkline_ascii({}, 10).empty());
  EXPECT_TRUE(marketsim::ui::build_sparkline_ascii({1.0, 2.0, 3.0}, 0).empty());
  const std::string flat = marketsim::ui::build_sparkline_ascii({5.0, 5.0, 5.0}, 8);
  EXPECT_EQ(flat.size(), 3u);
}

TEST(DashboardFormat, SparklineScalesRange) {
  const std::vector<double> v{0.0, 10.0, 20.0};
  const std::string s = marketsim::ui::build_sparkline_ascii(v, 3);
  ASSERT_EQ(s.size(), 3u);
  EXPECT_NE(s[0], s[2]);
}

TEST(DashboardFormat, TruncateAscii) {
  EXPECT_EQ(marketsim::ui::truncate_ascii("AAPL", 10), "AAPL");
  EXPECT_EQ(marketsim::ui::truncate_ascii("ABCDEFGHIJK", 10), "ABCDEFG...");
}

TEST(DashboardPrepare, EmptyOrderbookAndTrades) {
  marketsim::view::SimulatorSnapshot snap;
  snap.focus_symbol = "AAPL";
  snap.sim_time = 42;
  const auto m = marketsim::ui::prepare_dashboard_render(snap, {});
  EXPECT_TRUE(m.orderbook_empty);
  EXPECT_TRUE(m.trades_empty);
  EXPECT_TRUE(m.positions_empty);
  EXPECT_TRUE(m.open_orders_empty);
  EXPECT_EQ(m.last_str, "—");
  EXPECT_EQ(m.mid_str, "—");
  EXPECT_EQ(m.spread_str, "n/a");
}

TEST(ViewSnapshot, CaptureFromSimulatorService) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 10'000'000);
  const auto snap = svc.capture_view_snapshot("AAPL", 4, 2);
  EXPECT_EQ(snap.focus_symbol, "AAPL");
  EXPECT_TRUE(snap.trades.empty());
  EXPECT_TRUE(snap.open_orders.empty());
  EXPECT_FALSE(snap.last_price_cents.has_value());
}

TEST(ViewSnapshot, IncludesLastPriceAndOpenOrders) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 100'000'000, {"AAPL", "MSFT"});
  svc.apply_mark_from_tick(marketsim::domain::Tick{.symbol = "AAPL",
                                                   .last_price = 18'000,
                                                   .last_size = 1,
                                                   .cumulative_volume = 1,
                                                   .time = 0});
  const marketsim::matching::SubmitRequest buy{.symbol = "MSFT",
                                               .side = marketsim::domain::Side::Buy,
                                               .type = marketsim::domain::OrderType::Limit,
                                               .limit_price = 40'000,
                                               .quantity = 3};
  ASSERT_EQ(svc.place_order(buy, std::nullopt).first, 200);

  const auto snap = svc.capture_view_snapshot("AAPL", 4, 2);
  ASSERT_TRUE(snap.last_price_cents.has_value());
  EXPECT_EQ(*snap.last_price_cents, 18'000);
  ASSERT_EQ(snap.open_orders.size(), 1u);
  EXPECT_EQ(snap.open_orders[0].symbol, "MSFT");
  EXPECT_TRUE(snap.open_orders[0].buy);
  EXPECT_EQ(snap.open_orders[0].remaining, 3);

  const auto m = marketsim::ui::prepare_dashboard_render(snap, {});
  EXPECT_EQ(m.last_str, "$180.00");
  EXPECT_FALSE(m.open_orders_empty);
  ASSERT_EQ(m.open_order_table.size(), 1u);
  EXPECT_EQ(m.open_order_table[0][2], "buy");
  EXPECT_EQ(m.open_order_table[0][3], "$400.00");
}

TEST(DashboardPrepare, PopulatedSnapshotStrings) {
  marketsim::view::SimulatorSnapshot snap;
  snap.focus_symbol = "AAPL";
  snap.sim_time = 9;
  snap.best_bid_cents = 100;
  snap.best_ask_cents = 102;
  snap.mid_price_cents = 101;
  snap.bids.push_back({100, 10, 2});
  snap.asks.push_back({102, 5, 1});
  snap.cash_cents = 1'000'000;
  snap.equity_cents = 1'050'000;
  snap.total_realized_pnl_cents = -100;
  snap.total_unrealized_pnl_cents = 200;
  snap.positions.push_back({"AAPL", 10, 100, 50, 1010});
  snap.trades.push_back({1, "AAPL", 101, 5, true, 8});

  std::vector<double> hist{100.0, 101.0, 102.0};
  const auto m = marketsim::ui::prepare_dashboard_render(snap, hist);
  EXPECT_FALSE(m.orderbook_empty);
  EXPECT_FALSE(m.trades_empty);
  EXPECT_FALSE(m.positions_empty);
  EXPECT_EQ(m.sparkline_line.find("(no mid history yet)"), std::string::npos);
  EXPECT_EQ(m.bid_table.size(), 1u);
  EXPECT_EQ(m.ask_table.size(), 1u);
}
