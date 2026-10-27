#include "marketsim/ui/dashboard_prepare.hpp"

#include "marketsim/ui/dashboard_format.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace marketsim::ui {

namespace {

[[nodiscard]] std::string format_sim_time(const std::uint64_t t) {
  std::ostringstream os;
  os << "sim t=" << t;
  return std::move(os).str();
}

}

DashboardRenderModel prepare_dashboard_render(const marketsim::view::SimulatorSnapshot& snap,
                                                const std::vector<double>& mid_history_cents) {
  DashboardRenderModel m;
  const std::time_t tt = std::chrono::system_clock::to_time_t(snap.captured_at);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &tt);
#else
  localtime_r(&tt, &local);
#endif
  {
    std::ostringstream os;
    os << std::put_time(&local, "%H:%M:%S");
    m.clock_hhmmss = std::move(os).str();
  }

  m.symbol_line = truncate_ascii(snap.focus_symbol, 12);
  m.sim_time_str = format_sim_time(snap.sim_time);
  m.cash_str = format_usd_cents(snap.cash_cents);
  m.equity_str = format_usd_cents(snap.equity_cents);
  m.realized_str = format_signed_usd_cents(snap.total_realized_pnl_cents);
  m.unrealized_str = format_signed_usd_cents(snap.total_unrealized_pnl_cents);

  if (snap.last_price_cents.has_value()) {
    m.last_str = format_usd_cents(*snap.last_price_cents);
  } else {
    m.last_str = "—";
  }

  if (snap.mid_price_cents.has_value()) {
    m.mid_str = format_usd_cents(*snap.mid_price_cents);
  } else {
    m.mid_str = "—";
  }

  if (snap.best_bid_cents.has_value() && snap.best_ask_cents.has_value()) {
    const std::int64_t spr = *snap.best_ask_cents - *snap.best_bid_cents;
    m.spread_str = format_usd_cents(spr);
  } else {
    m.spread_str = "n/a";
  }

  m.orderbook_empty = snap.bids.empty() && snap.asks.empty();
  for (const auto& row : snap.bids) {
    std::array<std::string, 3> line;
    line[0] = format_usd_cents(row.price_cents);
    line[1] = std::to_string(row.total_quantity);
    line[2] = std::to_string(row.order_count);
    m.bid_table.push_back(std::move(line));
  }
  for (const auto& row : snap.asks) {
    std::array<std::string, 3> line;
    line[0] = format_usd_cents(row.price_cents);
    line[1] = std::to_string(row.total_quantity);
    line[2] = std::to_string(row.order_count);
    m.ask_table.push_back(std::move(line));
  }

  m.positions_empty = snap.positions.empty();
  for (const auto& p : snap.positions) {
    std::array<std::string, 5> line;
    line[0] = truncate_ascii(p.symbol, 6);
    line[1] = std::to_string(p.quantity);
    line[2] = format_usd_cents(p.avg_entry_cents);
    line[3] = format_signed_usd_cents(p.unrealized_pnl_cents);
    line[4] = format_usd_cents(p.market_value_cents);
    m.position_table.push_back(std::move(line));
  }

  m.trades_empty = snap.trades.empty();
  for (const auto& trow : snap.trades) {
    std::array<std::string, 5> line;
    line[0] = std::to_string(trow.trade_id);
    line[1] = truncate_ascii(trow.symbol, 5);
    line[2] = trow.trader_buy ? "buy" : "sel";
    line[3] = format_usd_cents(trow.price_cents);
    line[4] = std::to_string(trow.quantity);
    m.trade_table.push_back(std::move(line));
  }

  m.open_orders_empty = snap.open_orders.empty();
  for (const auto& o : snap.open_orders) {
    std::array<std::string, 5> line;
    line[0] = std::to_string(o.order_id);
    line[1] = truncate_ascii(o.symbol, 5);
    line[2] = o.buy ? "buy" : "sel";
    line[3] = format_usd_cents(o.price_cents);
    line[4] = std::to_string(o.remaining);
    m.open_order_table.push_back(std::move(line));
  }

  if (mid_history_cents.empty()) {
    m.sparkline_line = "(no mid history yet)";
  } else {
    m.sparkline_line = build_sparkline_ascii(mid_history_cents, 48);
  }
  return m;
}

}
