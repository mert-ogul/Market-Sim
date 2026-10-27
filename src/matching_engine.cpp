#include "marketsim/matching/engine.hpp"

#include "marketsim/domain/order.hpp"
#include "marketsim/orderbook/order_book.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace marketsim::matching {

namespace {

[[nodiscard]] bool buy_crosses(const orderbook::OrderBook& book,
                             const std::optional<domain::PriceCents> limit_price) {
  const auto ask = book.best_ask();
  if (!ask.has_value()) {
    return false;
  }
  if (!limit_price.has_value()) {
    return true;
  }
  return *ask <= *limit_price;
}

[[nodiscard]] bool sell_crosses(const orderbook::OrderBook& book,
                             const std::optional<domain::PriceCents> limit_price) {
  const auto bid = book.best_bid();
  if (!bid.has_value()) {
    return false;
  }
  if (!limit_price.has_value()) {
    return true;
  }
  return *bid >= *limit_price;
}

[[nodiscard]] domain::Trade make_trade(const domain::OrderId passive_id,
                                       const domain::OrderId aggressive_id,
                                       const domain::Symbol& symbol, const domain::Side aggressive_side,
                                       const domain::PriceCents price, const domain::Quantity qty,
                                       const domain::SimTime time) {
  domain::Trade trade;
  trade.passive_order_id = passive_id;
  trade.aggressive_order_id = aggressive_id;
  trade.symbol = symbol;
  trade.aggressive_side = aggressive_side;
  trade.price = price;
  trade.quantity = qty;
  trade.time = time;
  return trade;
}

}

MatchingEngine::MatchingEngine(const domain::SimTime start_time) : sim_time_(start_time) {}

orderbook::OrderBook& MatchingEngine::book_for(const domain::Symbol& symbol) {
  auto [it, inserted] = books_.try_emplace(symbol, symbol);
  return it->second;
}

domain::Quote MatchingEngine::make_quote(const domain::Symbol& symbol,
                                           const orderbook::OrderBook& book) const {
  const auto tob = book.top_of_book();
  domain::Quote q;
  q.symbol = symbol;
  q.bid_price = tob.bid_price;
  q.ask_price = tob.ask_price;
  q.bid_size = tob.bid_size;
  q.ask_size = tob.ask_size;
  q.time = sim_time_;
  return q;
}

SubmitResult MatchingEngine::submit_order(const SubmitRequest& request) {
  SubmitResult out;

  if (request.quantity <= 0) {
    out.reject = SubmitRejectReason::InvalidQuantity;
    return out;
  }
  if (request.type == domain::OrderType::Limit &&
      (!request.limit_price.has_value() || *request.limit_price <= 0)) {
    out.reject = SubmitRejectReason::InvalidLimitPrice;
    return out;
  }
  if (request.type == domain::OrderType::Market && request.limit_price.has_value()) {
    out.reject = SubmitRejectReason::InvalidLimitPrice;
    return out;
  }

  auto& book = book_for(request.symbol);
  const domain::OrderId id = next_order_id_++;
  order_to_symbol_.emplace(id, request.symbol);

  domain::Order order;
  order.id = id;
  order.symbol = request.symbol;
  order.side = request.side;
  order.type = request.type;
  order.limit_price = request.limit_price;
  order.quantity = request.quantity;
  order.remaining = request.quantity;
  order.created_time = sim_time_;

  if (request.side == domain::Side::Buy) {
    auto& trades = out.trades;
    while (order.remaining > 0 && buy_crosses(book, request.limit_price)) {
      const auto passive_id = book.front_order_id_best_ask();
      if (!passive_id.has_value()) {
        break;
      }
      const auto passive_rem = book.remaining_quantity(*passive_id);
      if (!passive_rem.has_value() || *passive_rem <= 0) {
        break;
      }
      const auto ask_px = book.best_ask();
      if (!ask_px.has_value()) {
        break;
      }
      if (request.limit_price.has_value() && *ask_px > *request.limit_price) {
        break;
      }
      const domain::Quantity fill_qty = std::min(order.remaining, *passive_rem);
      if (!book.apply_fill(*passive_id, fill_qty)) {
        break;
      }
      TradeFill fill;
      fill.trade_id = next_trade_id_++;
      fill.trade = make_trade(*passive_id, id, request.symbol, domain::Side::Buy, *ask_px,
                              fill_qty, sim_time_);
      trades.push_back(fill);
      order.remaining -= fill_qty;
      ++sim_time_;
    }
  } else {
    auto& trades = out.trades;
    while (order.remaining > 0 && sell_crosses(book, request.limit_price)) {
      const auto passive_id = book.front_order_id_best_bid();
      if (!passive_id.has_value()) {
        break;
      }
      const auto passive_rem = book.remaining_quantity(*passive_id);
      if (!passive_rem.has_value() || *passive_rem <= 0) {
        break;
      }
      const auto bid_px = book.best_bid();
      if (!bid_px.has_value()) {
        break;
      }
      if (request.limit_price.has_value() && *bid_px < *request.limit_price) {
        break;
      }
      const domain::Quantity fill_qty = std::min(order.remaining, *passive_rem);
      if (!book.apply_fill(*passive_id, fill_qty)) {
        break;
      }
      TradeFill fill;
      fill.trade_id = next_trade_id_++;
      fill.trade = make_trade(*passive_id, id, request.symbol, domain::Side::Sell, *bid_px,
                              fill_qty, sim_time_);
      trades.push_back(fill);
      order.remaining -= fill_qty;
      ++sim_time_;
    }
  }

  if (request.type == domain::OrderType::Market) {
    if (out.trades.empty()) {
      out.reject = SubmitRejectReason::MarketNoLiquidity;
      order_to_symbol_.erase(id);
      out.quote_after = make_quote(request.symbol, book);
      return out;
    }
    order_to_symbol_.erase(id);
    out.accepted = true;
    out.order_id = id;
    out.resting_quantity = 0;
    out.quote_after = make_quote(request.symbol, book);
    return out;
  }

  if (order.remaining > 0) {
    if (!book.insert_limit_order(order)) {
      out.accepted = false;
      out.reject = SubmitRejectReason::InvalidQuantity;
      order_to_symbol_.erase(id);
      out.quote_after = make_quote(request.symbol, book);
      return out;
    }
    out.accepted = true;
    out.order_id = id;
    out.resting_quantity = order.remaining;
    out.quote_after = make_quote(request.symbol, book);
    return out;
  }

  order_to_symbol_.erase(id);
  out.accepted = true;
  out.order_id = id;
  out.resting_quantity = 0;
  out.quote_after = make_quote(request.symbol, book);
  return out;
}

const orderbook::OrderBook* MatchingEngine::find_book(const domain::Symbol& symbol) const {
  const auto it = books_.find(symbol);
  if (it == books_.end()) {
    return nullptr;
  }
  return &it->second;
}

domain::Quote MatchingEngine::quote_snapshot(const domain::Symbol& symbol) const {
  const orderbook::OrderBook* book = find_book(symbol);
  domain::Quote q;
  q.symbol = symbol;
  q.time = sim_time_;
  if (book == nullptr) {
    return q;
  }
  return make_quote(symbol, *book);
}

std::vector<orderbook::OrderBook::DepthLevel> MatchingEngine::orderbook_bids(
    const domain::Symbol& symbol, const std::size_t max_levels) const {
  const orderbook::OrderBook* book = find_book(symbol);
  if (book == nullptr) {
    return {};
  }
  return book->depth_bids(max_levels);
}

std::vector<orderbook::OrderBook::DepthLevel> MatchingEngine::orderbook_asks(
    const domain::Symbol& symbol, const std::size_t max_levels) const {
  const orderbook::OrderBook* book = find_book(symbol);
  if (book == nullptr) {
    return {};
  }
  return book->depth_asks(max_levels);
}

CancelResult MatchingEngine::cancel_order(const domain::OrderId id) {
  CancelResult out;
  const auto it = order_to_symbol_.find(id);
  if (it == order_to_symbol_.end()) {
    return out;
  }
  const domain::Symbol symbol = it->second;

  auto book_it = books_.find(symbol);
  if (book_it == books_.end()) {
    return out;
  }
  orderbook::OrderBook& book = book_it->second;
  if (!book.cancel_order(id)) {
    return out;
  }

  order_to_symbol_.erase(it);
  out.cancelled = true;
  out.quote_after = make_quote(symbol, book);
  ++sim_time_;
  return out;
}

}
