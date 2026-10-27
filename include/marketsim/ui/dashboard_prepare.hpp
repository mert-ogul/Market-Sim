#pragma once

#include "marketsim/view/simulator_snapshot.hpp"

#include <array>
#include <string>
#include <vector>

namespace marketsim::ui {

/// Pre-formatted strings for FTXUI elements (built from a snapshot + price history).
struct DashboardRenderModel {
  std::string clock_hhmmss;
  std::string symbol_line;
  std::string sim_time_str;
  std::string cash_str;
  std::string equity_str;
  std::string realized_str;
  std::string unrealized_str;
  std::string last_str;
  std::string mid_str;
  std::string spread_str;
  /// Header row + bid rows (price, qty, orders).
  std::vector<std::array<std::string, 3>> bid_table;
  std::vector<std::array<std::string, 3>> ask_table;
  std::vector<std::array<std::string, 5>> position_table;
  std::vector<std::array<std::string, 5>> trade_table;
  std::vector<std::array<std::string, 5>> open_order_table;
  std::string sparkline_line;
  bool orderbook_empty{true};
  bool positions_empty{true};
  bool trades_empty{true};
  bool open_orders_empty{true};
};

/// Pure transformation: snapshot + optional mid history → render model (testable).
[[nodiscard]] DashboardRenderModel prepare_dashboard_render(const marketsim::view::SimulatorSnapshot& snap,
                                                            const std::vector<double>& mid_history_cents);

}
