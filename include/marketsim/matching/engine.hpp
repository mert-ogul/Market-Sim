#pragma once

#include "marketsim/domain/market_data.hpp"
#include "marketsim/matching/types.hpp"
#include "marketsim/orderbook/order_book.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace marketsim::matching {

/// Central limit order book matcher: per-symbol books, price-time priority, IOC market orders.
/// Emits strongly typed fills and quote snapshots; callers can forward these to an event bus.
class MatchingEngine {
public:
  explicit MatchingEngine(domain::SimTime start_time = 0);

  [[nodiscard]] SubmitResult submit_order(const SubmitRequest& request);

  /// Cancels a resting limit order. Returns false if unknown or already completed.
  [[nodiscard]] CancelResult cancel_order(domain::OrderId id);

  [[nodiscard]] domain::SimTime next_sim_time() const noexcept { return sim_time_; }

  /// Top-of-book for a symbol (empty sides if no book or no liquidity).
  [[nodiscard]] domain::Quote quote_snapshot(const domain::Symbol& symbol) const;

  /// Depth snapshot; empty vectors if the symbol has no book yet.
  [[nodiscard]] std::vector<orderbook::OrderBook::DepthLevel> orderbook_bids(
      const domain::Symbol& symbol, std::size_t max_levels) const;
  [[nodiscard]] std::vector<orderbook::OrderBook::DepthLevel> orderbook_asks(
      const domain::Symbol& symbol, std::size_t max_levels) const;

private:
  [[nodiscard]] const orderbook::OrderBook* find_book(const domain::Symbol& symbol) const;
  [[nodiscard]] orderbook::OrderBook& book_for(const domain::Symbol& symbol);

  [[nodiscard]] domain::Quote make_quote(const domain::Symbol& symbol,
                                         const orderbook::OrderBook& book) const;

  std::unordered_map<domain::Symbol, orderbook::OrderBook> books_;
  std::unordered_map<domain::OrderId, domain::Symbol> order_to_symbol_;
  domain::OrderId next_order_id_{1};
  domain::TradeId next_trade_id_{1};
  domain::SimTime sim_time_{0};
};

}
