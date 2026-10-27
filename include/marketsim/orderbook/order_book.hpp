#pragma once

#include "marketsim/domain/order.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace marketsim::orderbook {

/// Single-symbol limit order book: price-time priority with O(1) cancel/fill by id.
/// One instance per symbol; the matching layer composes multiple books.
class OrderBook {
public:
  explicit OrderBook(domain::Symbol symbol);
  ~OrderBook();

  OrderBook(OrderBook&& other) noexcept;
  OrderBook& operator=(OrderBook&& other) noexcept;

  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;

  [[nodiscard]] const domain::Symbol& symbol() const noexcept;

  /// Inserts a resting limit order. Fails on duplicate id, symbol mismatch, or invalid fields.
  [[nodiscard]] bool insert_limit_order(const domain::Order& order);

  /// Removes a resting order by id. Returns false if the id is unknown (no-op).
  [[nodiscard]] bool cancel_order(domain::OrderId id);

  /// Reduces remaining size after a trade; removes the order if remaining hits zero.
  /// Fails if id unknown, fill_qty invalid, or fill_qty exceeds remaining.
  [[nodiscard]] bool apply_fill(domain::OrderId id, domain::Quantity fill_qty);

  [[nodiscard]] std::optional<domain::PriceCents> best_bid() const;
  [[nodiscard]] std::optional<domain::PriceCents> best_ask() const;

  /// ask - bid when both sides exist; otherwise nullopt.
  [[nodiscard]] std::optional<domain::PriceCents> spread() const;

  /// Aggregate remaining quantity at the best bid price (0 if no bids).
  [[nodiscard]] domain::Quantity bid_volume_at_best() const;
  /// Aggregate remaining quantity at the best ask price (0 if no asks).
  [[nodiscard]] domain::Quantity ask_volume_at_best() const;

  struct TopOfBook {
    std::optional<domain::PriceCents> bid_price;
    domain::Quantity bid_size{0};
    std::optional<domain::PriceCents> ask_price;
    domain::Quantity ask_size{0};
  };
  [[nodiscard]] TopOfBook top_of_book() const;

  struct DepthLevel {
    domain::PriceCents price{0};
    domain::Quantity total_quantity{0};
    std::size_t order_count{0};
  };

  /// Near-to-touch first: bids from highest price, asks from lowest price.
  [[nodiscard]] std::vector<DepthLevel> depth_bids(std::size_t max_levels) const;
  [[nodiscard]] std::vector<DepthLevel> depth_asks(std::size_t max_levels) const;

  [[nodiscard]] std::size_t order_count() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  /// Oldest order id at the best ask (FIFO); for matching against incoming buys.
  [[nodiscard]] std::optional<domain::OrderId> front_order_id_best_ask() const;
  /// Oldest order id at the best bid (FIFO); for matching against incoming sells.
  [[nodiscard]] std::optional<domain::OrderId> front_order_id_best_bid() const;

  /// Returns nullptr for an unknown id. Pointer is invalidated by any mutation of the book.
  [[nodiscard]] const domain::Order* find_order(domain::OrderId id) const;

  /// Remaining quantity for a resting order, if present.
  [[nodiscard]] std::optional<domain::Quantity> remaining_quantity(domain::OrderId id) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
