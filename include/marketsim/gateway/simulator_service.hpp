#pragma once

#include "marketsim/domain/market_maker_settings.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/domain/trade.hpp"
#include "marketsim/domain/types.hpp"
#include "marketsim/matching/engine.hpp"
#include "marketsim/matching/types.hpp"
#include "marketsim/portfolio/portfolio.hpp"
#include "marketsim/portfolio/risk_manager.hpp"
#include "marketsim/view/simulator_snapshot.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace marketsim::gateway {

/// Thread-safe façade over engine, portfolio, and risk. All public methods are serialized with
/// one mutex; intended to be driven from a single httplib listener thread or a small pool.
class SimulatorService {
public:
  /// Restricts orders and market data to these symbols; empty allows any.
  explicit SimulatorService(domain::RiskLimits limits, domain::PriceCents initial_cash_cents,
                            std::vector<domain::Symbol> tradable_symbols = {},
                            domain::MarketMakerSettings market_maker = {});

  SimulatorService(const SimulatorService&) = delete;
  SimulatorService& operator=(const SimulatorService&) = delete;
  SimulatorService(SimulatorService&&) = delete;
  SimulatorService& operator=(SimulatorService&&) = delete;

  [[nodiscard]] bool is_tradable(const domain::Symbol& symbol) const;

  [[nodiscard]] nlohmann::json orderbook_json(const domain::Symbol& symbol,
                                              std::size_t max_levels = 32) const;

  [[nodiscard]] nlohmann::json quote_json(const domain::Symbol& symbol) const;

  /// Runs risk check, submits to engine, updates portfolio and references, records trades.
  [[nodiscard]] std::pair<int, nlohmann::json> place_order(const matching::SubmitRequest& request,
                                                           const std::optional<std::string>& client_id);

  [[nodiscard]] std::pair<int, nlohmann::json> cancel_order(domain::OrderId id);

  [[nodiscard]] nlohmann::json portfolio_json() const;

  /// Resting trader orders: id, symbol, side, limit price, size and remaining.
  [[nodiscard]] nlohmann::json open_orders_json() const;

  /// Configured symbols with last feed price (null until the first tick).
  [[nodiscard]] nlohmann::json symbols_json() const;

  [[nodiscard]] nlohmann::json trades_json(std::size_t max_rows = 500) const;

  /// Single-lock read model for UIs and tooling (structured portfolio fields, no JSON parse).
  [[nodiscard]] view::SimulatorSnapshot capture_view_snapshot(const domain::Symbol& symbol,
                                                              std::size_t depth_levels,
                                                              std::size_t max_trade_rows) const;

  /// Applies MTM mark and risk reference from a feed tick, then requotes the market maker when enabled.
  void apply_mark_from_tick(const domain::Tick& tick);

private:
  void record_and_apply_fills(const matching::SubmitResult& result);
  void apply_trader_fill(domain::Side side, const domain::Trade& trade);
  void update_reference_from_quote(const domain::Quote& q);
  void requote_market_maker(const domain::Symbol& symbol, domain::PriceCents mid_cents);
  [[nodiscard]] nlohmann::json last_price_json(const domain::Symbol& symbol) const;

  const std::vector<domain::Symbol> tradable_symbols_;
  mutable std::mutex mu_;
  portfolio::Portfolio portfolio_;
  risk::RiskManager risk_;
  matching::MatchingEngine engine_;
  std::deque<std::pair<domain::TradeId, domain::Trade>> trades_;
  const domain::MarketMakerSettings market_maker_;
  std::unordered_map<domain::Symbol, std::vector<domain::OrderId>> market_maker_orders_;
  std::unordered_map<domain::Symbol, domain::PriceCents> last_prices_;
  static constexpr std::size_t kMaxTrades = 10'000;
};

}
