#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/market_info.hpp"
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

TEST(DashboardFormat, IndexValueAndPercent) {
  EXPECT_EQ(marketsim::ui::format_index_value(54306.814), "54,306.81");
  EXPECT_EQ(marketsim::ui::format_index_value(999.5), "999.50");
  EXPECT_EQ(marketsim::ui::format_index_value(-1234567.0), "-1,234,567.00");
  EXPECT_EQ(marketsim::ui::format_signed_percent(0.4812), "+0.48%");
  EXPECT_EQ(marketsim::ui::format_signed_percent(-1.2), "-1.20%");
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

TEST(DashboardPrepare, IndexLineAndWatchTable) {
  marketsim::view::SimulatorSnapshot snap;
  snap.index_name = "Dow 30";
  snap.index_value = 54306.81;
  snap.index_open_value = 54047.42;
  snap.watchlist.push_back(marketsim::view::WatchRow{
      .symbol = "AAPL",
      .name = "Apple",
      .last_price_cents = 34'465,
      .open_price_cents = 34'391,
      .position = 10,
  });
  snap.watchlist.push_back(marketsim::view::WatchRow{
      .symbol = "KO",
      .name = "Coca-Cola",
      .last_price_cents = 9'200,
      .open_price_cents = 9'270,
      .position = 0,
  });
  snap.watchlist.push_back(marketsim::view::WatchRow{
      .symbol = "NKE",
      .name = "Nike",
  });

  const auto m = marketsim::ui::prepare_dashboard_render(snap, {});
  EXPECT_EQ(m.index_line, "Dow 30  54,306.81  +259.39 (+0.48%)");
  EXPECT_EQ(m.index_short, "Dow 30 54,306.81 +0.48%");
  EXPECT_EQ(m.index_direction, 1);
  ASSERT_EQ(m.watch_table.size(), 3u);
  ASSERT_EQ(m.watch_direction.size(), 3u);
  EXPECT_EQ(m.watch_table[0][2], "$344.65");
  EXPECT_EQ(m.watch_table[0][3], "+0.22%");
  EXPECT_EQ(m.watch_table[0][4], "10");
  EXPECT_EQ(m.watch_direction[1], -1);
  EXPECT_TRUE(m.watch_table[1][4].empty());
  EXPECT_EQ(m.watch_table[2][3], "—");
}

TEST(ViewSnapshot, WatchlistFollowsConfiguredSymbols) {
  marketsim::domain::RiskLimits limits{};
  marketsim::domain::MarketInfo info;
  info.company_names = {{"AAPL", "Apple"}};
  info.index_name = "Pair";
  info.index_divisor = 1.0;
  marketsim::gateway::SimulatorService svc(limits, 100'000'000, {"AAPL", "MSFT"},
                                           marketsim::domain::MarketMakerSettings{}, std::move(info));
  svc.apply_mark_from_tick(marketsim::domain::Tick{.symbol = "AAPL",
                                                   .last_price = 18'000,
                                                   .last_size = 1,
                                                   .cumulative_volume = 1,
                                                   .time = 0});
  svc.apply_mark_from_tick(marketsim::domain::Tick{.symbol = "MSFT",
                                                   .last_price = 42'000,
                                                   .last_size = 1,
                                                   .cumulative_volume = 1,
                                                   .time = 1});

  const auto snap = svc.capture_view_snapshot("AAPL", 4, 2);
  ASSERT_EQ(snap.watchlist.size(), 2u);
  EXPECT_EQ(snap.watchlist[0].name, "Apple");
  EXPECT_EQ(snap.watchlist[1].name, "MSFT");
  ASSERT_TRUE(snap.watchlist[1].open_price_cents.has_value());
  EXPECT_EQ(*snap.watchlist[1].open_price_cents, 42'000);
  ASSERT_TRUE(snap.index_value.has_value());
  EXPECT_DOUBLE_EQ(*snap.index_value, 600.0);
}
