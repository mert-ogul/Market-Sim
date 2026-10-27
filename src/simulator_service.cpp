#include "marketsim/gateway/simulator_service.hpp"

#include "marketsim/orderbook/order_book.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <utility>

namespace marketsim::gateway {

namespace {

[[nodiscard]] const char* risk_reason_string(const risk::RiskRejectReason r) {
  using risk::RiskRejectReason;
  switch (r) {
  case RiskRejectReason::None:
    return "none";
  case RiskRejectReason::InvalidRequest:
    return "invalid_request";
  case RiskRejectReason::UnknownSymbolReference:
    return "unknown_symbol_reference";
  case RiskRejectReason::OrderExceedsMaxSize:
    return "order_exceeds_max_size";
  case RiskRejectReason::PositionLimitExceeded:
    return "position_limit_exceeded";
  case RiskRejectReason::InsufficientCash:
    return "insufficient_cash";
  case RiskRejectReason::PriceOutsideCollar:
    return "price_outside_collar";
  case RiskRejectReason::AbsoluteMinMaxPrice:
    return "absolute_min_max_price";
  }
  return "unknown";
}

[[nodiscard]] const char* engine_reject_string(const matching::SubmitRejectReason r) {
  using matching::SubmitRejectReason;
  switch (r) {
  case SubmitRejectReason::None:
    return "none";
  case SubmitRejectReason::InvalidQuantity:
    return "invalid_quantity";
  case SubmitRejectReason::InvalidLimitPrice:
    return "invalid_limit_price";
  case SubmitRejectReason::MarketNoLiquidity:
    return "market_no_liquidity";
  }
  return "unknown";
}

[[nodiscard]] nlohmann::json quote_to_json(const domain::Quote& q) {
  nlohmann::json j;
  j["symbol"] = q.symbol;
  j["bid_price_cents"] = q.bid_price.has_value() ? nlohmann::json(*q.bid_price) : nlohmann::json(nullptr);
  j["ask_price_cents"] = q.ask_price.has_value() ? nlohmann::json(*q.ask_price) : nlohmann::json(nullptr);
  j["bid_size"] = q.bid_size;
  j["ask_size"] = q.ask_size;
  j["sim_time"] = q.time;
  return j;
}

[[nodiscard]] nlohmann::json depth_level_to_json(const orderbook::OrderBook::DepthLevel& d) {
  nlohmann::json j;
  j["price_cents"] = d.price;
  j["total_quantity"] = d.total_quantity;
  j["order_count"] = d.order_count;
  return j;
}

[[nodiscard]] nlohmann::json make_error_json(const std::string& code, const std::string& message) {
  nlohmann::json j;
  j["error"] = code;
  j["message"] = message;
  return j;
}

[[nodiscard]] domain::Side opposite(const domain::Side side) {
  return side == domain::Side::Buy ? domain::Side::Sell : domain::Side::Buy;
}

}

SimulatorService::SimulatorService(domain::RiskLimits limits, const domain::PriceCents initial_cash_cents,
                                   std::vector<domain::Symbol> tradable_symbols,
                                   domain::MarketMakerSettings market_maker)
    : tradable_symbols_(std::move(tradable_symbols)),
      portfolio_(initial_cash_cents),
      risk_(std::move(limits), portfolio_),
      market_maker_(market_maker) {}

bool SimulatorService::is_tradable(const domain::Symbol& symbol) const {
  return tradable_symbols_.empty() ||
         std::find(tradable_symbols_.begin(), tradable_symbols_.end(), symbol) !=
             tradable_symbols_.end();
}

void SimulatorService::update_reference_from_quote(const domain::Quote& q) {
  if (q.bid_price.has_value() && q.ask_price.has_value()) {
    risk_.set_reference(q.symbol, (*q.bid_price + *q.ask_price) / 2);
    return;
  }
  if (q.bid_price.has_value()) {
    risk_.set_reference(q.symbol, *q.bid_price);
    return;
  }
  if (q.ask_price.has_value()) {
    risk_.set_reference(q.symbol, *q.ask_price);
  }
}

void SimulatorService::apply_trader_fill(const domain::Side side, const domain::Trade& trade) {
  const bool applied = portfolio_.apply_fill(side, trade.symbol, trade.price, trade.quantity);
  assert(applied && "portfolio apply_fill must succeed for engine-emitted trades");
  (void)applied;
}

void SimulatorService::record_and_apply_fills(const matching::SubmitResult& result) {
  for (const matching::TradeFill& fill : result.trades) {
    trades_.push_back({fill.trade_id, fill.trade});
    if (trades_.size() > kMaxTrades) {
      trades_.pop_front();
    }
    const domain::Trade& trade = fill.trade;
    if (trade.aggressive_owner == domain::kTraderAccount) {
      apply_trader_fill(trade.aggressive_side, trade);
    }
    if (trade.passive_owner == domain::kTraderAccount) {
      apply_trader_fill(opposite(trade.aggressive_side), trade);
    }
    risk_.set_reference(fill.trade.symbol, fill.trade.price);
  }
  if (result.quote_after.has_value()) {
    update_reference_from_quote(*result.quote_after);
  }
}

nlohmann::json SimulatorService::orderbook_json(const domain::Symbol& symbol,
                                                  const std::size_t max_levels) const {
  std::lock_guard lock(mu_);
  nlohmann::json j;
  j["symbol"] = symbol;
  const auto bids = engine_.orderbook_bids(symbol, max_levels);
  const auto asks = engine_.orderbook_asks(symbol, max_levels);
  auto jb = nlohmann::json::array();
  for (const auto& row : bids) {
    jb.push_back(depth_level_to_json(row));
  }
  auto ja = nlohmann::json::array();
  for (const auto& row : asks) {
    ja.push_back(depth_level_to_json(row));
  }
  j["bids"] = std::move(jb);
  j["asks"] = std::move(ja);
  return j;
}

nlohmann::json SimulatorService::quote_json(const domain::Symbol& symbol) const {
  std::lock_guard lock(mu_);
  return quote_to_json(engine_.quote_snapshot(symbol));
}

std::pair<int, nlohmann::json> SimulatorService::place_order(const matching::SubmitRequest& request,
                                                               const std::optional<std::string>& client_id) {
  if (!is_tradable(request.symbol)) {
    return {422, make_error_json("unknown_symbol", "symbol is not configured: " + request.symbol)};
  }
  std::lock_guard lock(mu_);
  const risk::RiskCheckResult rr = risk_.check(request);
  if (!rr.allowed) {
    nlohmann::json j = make_error_json(risk_reason_string(rr.reason),
                                  rr.detail != nullptr ? std::string(rr.detail) : std::string("rejected"));
    j["risk_reason"] = risk_reason_string(rr.reason);
    return {422, std::move(j)};
  }

  const matching::SubmitResult sr = engine_.submit_order(request);
  if (!sr.accepted) {
    nlohmann::json j =
        make_error_json(engine_reject_string(sr.reject), "matching engine rejected the order");
    j["engine_reason"] = engine_reject_string(sr.reject);
    return {422, std::move(j)};
  }

  record_and_apply_fills(sr);

  nlohmann::json out;
  out["accepted"] = true;
  out["order_id"] = sr.order_id;
  out["resting_quantity"] = sr.resting_quantity;
  if (client_id.has_value()) {
    out["client_id"] = *client_id;
  }
  auto arr = nlohmann::json::array();
  for (const matching::TradeFill& f : sr.trades) {
    nlohmann::json t;
    t["trade_id"] = f.trade_id;
    t["symbol"] = f.trade.symbol;
    t["price_cents"] = f.trade.price;
    t["quantity"] = f.trade.quantity;
    t["aggressive_side"] = f.trade.aggressive_side == domain::Side::Buy ? "buy" : "sell";
    t["passive_order_id"] = f.trade.passive_order_id;
    t["aggressive_order_id"] = f.trade.aggressive_order_id;
    t["sim_time"] = f.trade.time;
    arr.push_back(std::move(t));
  }
  out["trades"] = std::move(arr);
  out["self_trade_cancelled_order_ids"] = sr.self_trade_cancelled_ids;
  if (sr.quote_after.has_value()) {
    out["quote"] = quote_to_json(*sr.quote_after);
  }
  return {200, std::move(out)};
}

std::pair<int, nlohmann::json> SimulatorService::cancel_order(const domain::OrderId id) {
  std::lock_guard lock(mu_);
  const matching::CancelResult cr = engine_.cancel_order(id);
  if (!cr.cancelled) {
    nlohmann::json j = make_error_json("not_found", "order id not found or not cancellable");
    return {404, std::move(j)};
  }
  if (cr.quote_after.has_value()) {
    update_reference_from_quote(*cr.quote_after);
  }
  nlohmann::json out;
  out["cancelled"] = true;
  out["order_id"] = id;
  if (cr.quote_after.has_value()) {
    out["quote"] = quote_to_json(*cr.quote_after);
  }
  return {200, std::move(out)};
}

nlohmann::json SimulatorService::portfolio_json() const {
  std::lock_guard lock(mu_);
  return portfolio_.to_json();
}

nlohmann::json SimulatorService::trades_json(const std::size_t max_rows) const {
  std::lock_guard lock(mu_);
  nlohmann::json arr = nlohmann::json::array();
  std::size_t n = 0;
  for (auto it = trades_.rbegin(); it != trades_.rend() && n < max_rows; ++it, ++n) {
    nlohmann::json row;
    row["trade_id"] = it->first;
    row["symbol"] = it->second.symbol;
    row["price_cents"] = it->second.price;
    row["quantity"] = it->second.quantity;
    row["aggressive_side"] = it->second.aggressive_side == domain::Side::Buy ? "buy" : "sell";
    row["sim_time"] = it->second.time;
    arr.push_back(std::move(row));
  }
  nlohmann::json out;
  out["trades"] = std::move(arr);
  return out;
}

view::SimulatorSnapshot SimulatorService::capture_view_snapshot(const domain::Symbol& symbol,
                                                                const std::size_t depth_levels,
                                                                const std::size_t max_trade_rows) const {
  std::lock_guard lock(mu_);
  view::SimulatorSnapshot snap;
  snap.captured_at = std::chrono::system_clock::now();
  snap.focus_symbol = symbol;

  const domain::Quote q = engine_.quote_snapshot(symbol);
  snap.sim_time = q.time;
  snap.best_bid_cents = q.bid_price;
  snap.best_ask_cents = q.ask_price;
  if (q.bid_price.has_value() && q.ask_price.has_value()) {
    snap.mid_price_cents = (*q.bid_price + *q.ask_price) / 2;
  } else if (q.bid_price.has_value()) {
    snap.mid_price_cents = *q.bid_price;
  } else if (q.ask_price.has_value()) {
    snap.mid_price_cents = *q.ask_price;
  }

  for (const auto& row : engine_.orderbook_bids(symbol, depth_levels)) {
    snap.bids.push_back(view::BookDepthRow{
        .price_cents = row.price, .total_quantity = row.total_quantity, .order_count = row.order_count});
  }
  for (const auto& row : engine_.orderbook_asks(symbol, depth_levels)) {
    snap.asks.push_back(view::BookDepthRow{
        .price_cents = row.price, .total_quantity = row.total_quantity, .order_count = row.order_count});
  }

  snap.cash_cents = portfolio_.cash_cents();
  snap.equity_cents = portfolio_.equity_cents();
  snap.total_realized_pnl_cents = portfolio_.total_realized_pnl_cents();
  snap.total_unrealized_pnl_cents = portfolio_.total_unrealized_pnl_cents();
  for (const auto& row : portfolio_.open_position_rows()) {
    snap.positions.push_back(view::PositionRow{.symbol = row.symbol,
                                                 .quantity = row.quantity,
                                                 .avg_entry_cents = row.avg_entry_cents,
                                                 .unrealized_pnl_cents = row.unrealized_pnl_cents,
                                                 .market_value_cents = row.market_value_cents});
  }

  std::size_t n = 0;
  for (auto it = trades_.rbegin(); it != trades_.rend() && n < max_trade_rows; ++it, ++n) {
    snap.trades.push_back(view::TradeTapeRow{
        .trade_id = it->first,
        .symbol = it->second.symbol,
        .price_cents = it->second.price,
        .quantity = it->second.quantity,
        .aggressive_buy = it->second.aggressive_side == domain::Side::Buy,
        .sim_time = it->second.time,
    });
  }
  return snap;
}

void SimulatorService::requote_market_maker(const domain::Symbol& symbol,
                                            const domain::PriceCents mid_cents) {
  auto& ids = market_maker_orders_[symbol];
  for (const domain::OrderId id : ids) {
    (void)engine_.cancel_order(id);  // false means the quote was already filled
  }
  ids.clear();

  const domain::PriceCents half_spread =
      std::max<domain::PriceCents>(1, mid_cents * market_maker_.half_spread_bps / 10'000);

  const std::array<std::pair<domain::Side, domain::PriceCents>, 2> quotes{{
      {domain::Side::Buy, std::max<domain::PriceCents>(1, mid_cents - half_spread)},
      {domain::Side::Sell, mid_cents + half_spread},
  }};

  for (const auto& [side, px] : quotes) {
    const matching::SubmitRequest req{.symbol = symbol,
                                      .side = side,
                                      .type = domain::OrderType::Limit,
                                      .limit_price = px,
                                      .quantity = market_maker_.quote_size,
                                      .owner = domain::kMarketMakerAccount};
    matching::SubmitResult result = engine_.submit_order(req);
    if (!result.accepted) {
      continue;
    }
    record_and_apply_fills(result);
    if (result.resting_quantity > 0) {
      ids.push_back(result.order_id);
    }
  }
}

void SimulatorService::apply_mark_from_tick(const domain::Tick& tick) {
  std::lock_guard lock(mu_);
  portfolio_.apply_mark(tick.symbol, tick.last_price);
  risk_.set_reference(tick.symbol, tick.last_price);
  if (market_maker_.enabled) {
    requote_market_maker(tick.symbol, tick.last_price);
  }
}

}
