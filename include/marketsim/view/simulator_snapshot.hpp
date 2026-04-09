#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace marketsim::view {

/// One aggregated depth level (price, total qty, order count) for read-only UIs.
struct BookDepthRow {
  std::int64_t price_cents{};
  std::int64_t total_quantity{};
  std::size_t order_count{};
};

/// Recent trade line for tables (distinct from `domain::Trade`).
struct TradeTapeRow {
  std::uint64_t trade_id{};
  std::string symbol;
  std::int64_t price_cents{};
  std::int64_t quantity{};
  bool aggressive_buy{};
  std::uint64_t sim_time{};
};

struct PositionRow {
  std::string symbol;
  std::int64_t quantity{};
  std::int64_t avg_entry_cents{};
  std::int64_t unrealized_pnl_cents{};
  std::int64_t market_value_cents{};
};

/// Immutable read model: engine + portfolio + tape under one logical snapshot.
struct SimulatorSnapshot {
  std::chrono::system_clock::time_point captured_at{};
  std::string focus_symbol;
  std::uint64_t sim_time{};
  std::optional<std::int64_t> best_bid_cents;
  std::optional<std::int64_t> best_ask_cents;
  std::optional<std::int64_t> mid_price_cents;
  std::vector<BookDepthRow> bids;
  std::vector<BookDepthRow> asks;
  std::int64_t cash_cents{};
  std::int64_t equity_cents{};
  std::int64_t total_realized_pnl_cents{};
  std::int64_t total_unrealized_pnl_cents{};
  std::vector<PositionRow> positions;
  std::vector<TradeTapeRow> trades;
};

}
