#include "marketsim/orderbook/order_book.hpp"

#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <utility>

namespace marketsim::orderbook {

namespace {

struct PriceLevel {
  std::list<domain::Order> orders;
};

/// Stable iterator into a price-level list enables O(1) cancel after O(1) id lookup.
struct OrderHandle {
  domain::Side side{};
  domain::PriceCents price{0};
  std::list<domain::Order>::iterator it{};
};

using BidBook =
    std::map<domain::PriceCents, PriceLevel, std::greater<domain::PriceCents>>;
using AskBook = std::map<domain::PriceCents, PriceLevel, std::less<domain::PriceCents>>;

void remove_resting_order(BidBook& bids, AskBook& asks, const OrderHandle& h) {
  if (h.side == domain::Side::Buy) {
    const auto level_it = bids.find(h.price);
    if (level_it == bids.end()) {
      return;
    }
    level_it->second.orders.erase(h.it);
    if (level_it->second.orders.empty()) {
      bids.erase(level_it);
    }
    return;
  }
  const auto level_it = asks.find(h.price);
  if (level_it == asks.end()) {
    return;
  }
  level_it->second.orders.erase(h.it);
  if (level_it->second.orders.empty()) {
    asks.erase(level_it);
  }
}

[[nodiscard]] domain::Quantity sum_remaining_at_best(const BidBook& bids) {
  if (bids.empty()) {
    return 0;
  }
  domain::Quantity total{0};
  for (const auto& o : bids.begin()->second.orders) {
    total += o.remaining;
  }
  return total;
}

[[nodiscard]] domain::Quantity sum_remaining_at_best(const AskBook& asks) {
  if (asks.empty()) {
    return 0;
  }
  domain::Quantity total{0};
  for (const auto& o : asks.begin()->second.orders) {
    total += o.remaining;
  }
  return total;
}

}

struct OrderBook::Impl {
  domain::Symbol symbol;
  BidBook bids;
  AskBook asks;
  std::unordered_map<domain::OrderId, OrderHandle> index;
};

OrderBook::OrderBook(domain::Symbol symbol) : impl_(std::make_unique<Impl>()) {
  impl_->symbol = std::move(symbol);
}

OrderBook::~OrderBook() = default;

OrderBook::OrderBook(OrderBook&&) noexcept = default;

OrderBook& OrderBook::operator=(OrderBook&&) noexcept = default;

const domain::Symbol& OrderBook::symbol() const noexcept { return impl_->symbol; }

bool OrderBook::insert_limit_order(const domain::Order& order) {
  if (order.symbol != impl_->symbol) {
    return false;
  }
  if (order.type != domain::OrderType::Limit) {
    return false;
  }
  if (!order.limit_price.has_value()) {
    return false;
  }
  if (order.remaining <= 0) {
    return false;
  }
  if (order.remaining > order.quantity) {
    return false;
  }
  if (impl_->index.contains(order.id)) {
    return false;
  }

  const domain::PriceCents px = *order.limit_price;
  OrderHandle handle;
  handle.side = order.side;
  handle.price = px;

  if (order.side == domain::Side::Buy) {
    auto& level = impl_->bids[px];
    level.orders.push_back(order);
    handle.it = std::prev(level.orders.end());
  } else {
    auto& level = impl_->asks[px];
    level.orders.push_back(order);
    handle.it = std::prev(level.orders.end());
  }

  impl_->index.emplace(order.id, handle);
  return true;
}

bool OrderBook::cancel_order(const domain::OrderId id) {
  const auto it = impl_->index.find(id);
  if (it == impl_->index.end()) {
    return false;
  }
  const OrderHandle handle = it->second;
  impl_->index.erase(it);
  remove_resting_order(impl_->bids, impl_->asks, handle);
  return true;
}

bool OrderBook::apply_fill(const domain::OrderId id, const domain::Quantity fill_qty) {
  if (fill_qty <= 0) {
    return false;
  }
  const auto it = impl_->index.find(id);
  if (it == impl_->index.end()) {
    return false;
  }

  OrderHandle handle = it->second;
  domain::Order& order = *handle.it;
  if (fill_qty > order.remaining) {
    return false;
  }

  order.remaining -= fill_qty;
  if (order.remaining > 0) {
    return true;
  }

  impl_->index.erase(it);
  remove_resting_order(impl_->bids, impl_->asks, handle);
  return true;
}

std::optional<domain::PriceCents> OrderBook::best_bid() const {
  if (impl_->bids.empty()) {
    return std::nullopt;
  }
  return impl_->bids.begin()->first;
}

std::optional<domain::PriceCents> OrderBook::best_ask() const {
  if (impl_->asks.empty()) {
    return std::nullopt;
  }
  return impl_->asks.begin()->first;
}

std::optional<domain::PriceCents> OrderBook::spread() const {
  const auto bid = best_bid();
  const auto ask = best_ask();
  if (!bid.has_value() || !ask.has_value()) {
    return std::nullopt;
  }
  return *ask - *bid;
}

domain::Quantity OrderBook::bid_volume_at_best() const {
  return sum_remaining_at_best(impl_->bids);
}

domain::Quantity OrderBook::ask_volume_at_best() const {
  return sum_remaining_at_best(impl_->asks);
}

OrderBook::TopOfBook OrderBook::top_of_book() const {
  TopOfBook t;
  if (!impl_->bids.empty()) {
    t.bid_price = impl_->bids.begin()->first;
    t.bid_size = sum_remaining_at_best(impl_->bids);
  }
  if (!impl_->asks.empty()) {
    t.ask_price = impl_->asks.begin()->first;
    t.ask_size = sum_remaining_at_best(impl_->asks);
  }
  return t;
}

std::vector<OrderBook::DepthLevel> OrderBook::depth_bids(const std::size_t max_levels) const {
  std::vector<DepthLevel> out;
  out.reserve(std::min(max_levels, impl_->bids.size()));
  std::size_t n = 0;
  for (const auto& [price, level] : impl_->bids) {
    if (n >= max_levels) {
      break;
    }
    DepthLevel row;
    row.price = price;
    for (const auto& o : level.orders) {
      row.total_quantity += o.remaining;
      row.order_count += 1;
    }
    out.push_back(row);
    ++n;
  }
  return out;
}

std::vector<OrderBook::DepthLevel> OrderBook::depth_asks(const std::size_t max_levels) const {
  std::vector<DepthLevel> out;
  out.reserve(std::min(max_levels, impl_->asks.size()));
  std::size_t n = 0;
  for (const auto& [price, level] : impl_->asks) {
    if (n >= max_levels) {
      break;
    }
    DepthLevel row;
    row.price = price;
    for (const auto& o : level.orders) {
      row.total_quantity += o.remaining;
      row.order_count += 1;
    }
    out.push_back(row);
    ++n;
  }
  return out;
}

std::size_t OrderBook::order_count() const noexcept { return impl_->index.size(); }

bool OrderBook::empty() const noexcept { return impl_->index.empty(); }

std::optional<domain::OrderId> OrderBook::front_order_id_best_ask() const {
  if (impl_->asks.empty()) {
    return std::nullopt;
  }
  const auto& lvl = impl_->asks.begin()->second.orders;
  if (lvl.empty()) {
    return std::nullopt;
  }
  return lvl.front().id;
}

std::optional<domain::OrderId> OrderBook::front_order_id_best_bid() const {
  if (impl_->bids.empty()) {
    return std::nullopt;
  }
  const auto& lvl = impl_->bids.begin()->second.orders;
  if (lvl.empty()) {
    return std::nullopt;
  }
  return lvl.front().id;
}

const domain::Order* OrderBook::find_order(const domain::OrderId id) const {
  const auto it = impl_->index.find(id);
  if (it == impl_->index.end()) {
    return nullptr;
  }
  return &(*it->second.it);
}

std::vector<domain::Order> OrderBook::orders_owned_by(const domain::AccountId owner) const {
  std::vector<domain::Order> out;
  const auto append_side = [&](const auto& side) {
    for (const auto& [price, level] : side) {
      (void)price;
      for (const domain::Order& order : level.orders) {
        if (order.owner == owner) {
          out.push_back(order);
        }
      }
    }
  };
  append_side(impl_->bids);
  append_side(impl_->asks);
  return out;
}

std::optional<domain::Quantity> OrderBook::remaining_quantity(const domain::OrderId id) const {
  const auto it = impl_->index.find(id);
  if (it == impl_->index.end()) {
    return std::nullopt;
  }
  return (*it->second.it).remaining;
}

}
