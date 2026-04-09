#include "marketsim/portfolio/portfolio.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace marketsim::portfolio {

namespace {

[[nodiscard]] bool multiply_would_overflow(const domain::Quantity qty, const domain::PriceCents px) {
  if (qty <= 0 || px == 0) {
    return false;
  }
  if (px > 0) {
    return qty > std::numeric_limits<domain::PriceCents>::max() / px;
  }
  return qty < std::numeric_limits<domain::PriceCents>::min() / px;
}

[[nodiscard]] domain::PriceCents product(const domain::Quantity qty, const domain::PriceCents px) {
  return qty * px;
}

[[nodiscard]] domain::PriceCents merge_weighted_avg(const domain::Quantity old_w,
                                                    const domain::PriceCents old_px,
                                                    const domain::Quantity add_w,
                                                    const domain::PriceCents add_px) {
  const domain::Quantity total = old_w + add_w;
  if (total <= 0) {
    return add_px;
  }
  const double v =
      (static_cast<double>(old_w) * static_cast<double>(old_px) +
       static_cast<double>(add_w) * static_cast<double>(add_px)) /
      static_cast<double>(total);
  return static_cast<domain::PriceCents>(std::llround(v));
}

[[nodiscard]] domain::PriceCents mark_for_unrealized(const PositionState& st) {
  if (st.quantity == 0) {
    return 0;
  }
  if (st.last_mark_cents != 0) {
    return st.last_mark_cents;
  }
  return st.avg_entry_cents;
}

}

Portfolio::Portfolio(const domain::PriceCents initial_cash_cents) : cash_cents_(initial_cash_cents) {}

PositionState& Portfolio::ensure_state(const domain::Symbol& symbol) { return positions_[symbol]; }

const PositionState* Portfolio::find_state(const domain::Symbol& symbol) const {
  const auto it = positions_.find(symbol);
  if (it == positions_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool Portfolio::apply_fill(const domain::Side side, const domain::Symbol& symbol,
                           const domain::PriceCents price_cents, const domain::Quantity quantity) {
  if (quantity <= 0 || price_cents < 0) {
    return false;
  }
  if (multiply_would_overflow(quantity, price_cents)) {
    return false;
  }
  const domain::PriceCents cash_delta = product(quantity, price_cents);

  PositionState& st = ensure_state(symbol);

  if (side == domain::Side::Buy) {
    cash_cents_ -= cash_delta;
    domain::Quantity q = quantity;

    if (st.quantity >= 0) {
      if (st.quantity == 0) {
        st.quantity = q;
        st.avg_entry_cents = price_cents;
      } else {
        st.avg_entry_cents = merge_weighted_avg(st.quantity, st.avg_entry_cents, q, price_cents);
        st.quantity += q;
      }
    } else {
      const domain::Quantity short_size = -st.quantity;
      const domain::Quantity cover = std::min(short_size, q);
      if (cover > 0) {
        st.realized_pnl_cents += (st.avg_entry_cents - price_cents) * cover;
        st.quantity += cover;
        q -= cover;
      }
      if (st.quantity == 0 && q > 0) {
        st.quantity = q;
        st.avg_entry_cents = price_cents;
      }
    }
  } else {
    cash_cents_ += cash_delta;
    domain::Quantity q = quantity;

    if (st.quantity <= 0) {
      if (st.quantity == 0) {
        st.quantity = -q;
        st.avg_entry_cents = price_cents;
      } else {
        const domain::Quantity old_short = -st.quantity;
        st.avg_entry_cents = merge_weighted_avg(old_short, st.avg_entry_cents, q, price_cents);
        st.quantity -= q;
      }
    } else {
      const domain::Quantity long_size = st.quantity;
      const domain::Quantity closing = std::min(long_size, q);
      if (closing > 0) {
        st.realized_pnl_cents += (price_cents - st.avg_entry_cents) * closing;
        st.quantity -= closing;
        q -= closing;
      }
      if (st.quantity == 0 && q > 0) {
        st.quantity = -q;
        st.avg_entry_cents = price_cents;
      }
    }
  }

  if (st.quantity == 0) {
    st.avg_entry_cents = 0;
  }
  st.last_mark_cents = price_cents;
  return true;
}

void Portfolio::apply_mark(const domain::Symbol& symbol, const domain::PriceCents mark_cents) {
  const auto it = positions_.find(symbol);
  if (it == positions_.end()) {
    return;
  }
  it->second.last_mark_cents = mark_cents;
}

domain::Quantity Portfolio::position_quantity(const domain::Symbol& symbol) const {
  const PositionState* st = find_state(symbol);
  return st ? st->quantity : 0;
}

domain::PriceCents Portfolio::avg_entry_cents(const domain::Symbol& symbol) const {
  const PositionState* st = find_state(symbol);
  return st ? st->avg_entry_cents : 0;
}

domain::PriceCents Portfolio::realized_pnl_cents(const domain::Symbol& symbol) const {
  const PositionState* st = find_state(symbol);
  return st ? st->realized_pnl_cents : 0;
}

domain::PriceCents Portfolio::unrealized_pnl_cents(const domain::Symbol& symbol) const {
  const PositionState* st = find_state(symbol);
  if (!st || st->quantity == 0) {
    return 0;
  }
  const domain::PriceCents m = mark_for_unrealized(*st);
  if (st->quantity > 0) {
    return st->quantity * (m - st->avg_entry_cents);
  }
  const domain::Quantity short_sz = -st->quantity;
  return short_sz * (st->avg_entry_cents - m);
}

domain::PriceCents Portfolio::market_value_cents(const domain::Symbol& symbol) const {
  const PositionState* st = find_state(symbol);
  if (!st || st->quantity == 0) {
    return 0;
  }
  const domain::PriceCents m = mark_for_unrealized(*st);
  return st->quantity * m;
}

domain::PriceCents Portfolio::total_realized_pnl_cents() const {
  domain::PriceCents sum = 0;
  for (const auto& [sym, st] : positions_) {
    (void)sym;
    sum += st.realized_pnl_cents;
  }
  return sum;
}

domain::PriceCents Portfolio::total_unrealized_pnl_cents() const {
  domain::PriceCents sum = 0;
  for (const auto& [sym, st] : positions_) {
    (void)st;
    sum += unrealized_pnl_cents(sym);
  }
  return sum;
}

domain::PriceCents Portfolio::equity_cents() const {
  domain::PriceCents mv = 0;
  for (const auto& [sym, st] : positions_) {
    (void)st;
    mv += market_value_cents(sym);
  }
  return cash_cents_ + mv;
}

std::vector<Portfolio::OpenPositionRow> Portfolio::open_position_rows() const {
  std::vector<OpenPositionRow> out;
  out.reserve(positions_.size());
  for (const auto& [sym, st] : positions_) {
    if (st.quantity == 0 && st.realized_pnl_cents == 0) {
      continue;
    }
    out.push_back(OpenPositionRow{.symbol = sym,
                                  .quantity = st.quantity,
                                  .avg_entry_cents = st.avg_entry_cents,
                                  .unrealized_pnl_cents = unrealized_pnl_cents(sym),
                                  .market_value_cents = market_value_cents(sym)});
  }
  return out;
}

nlohmann::json Portfolio::to_json() const {
  nlohmann::json j;
  j["cash_cents"] = cash_cents_;
  j["equity_cents"] = equity_cents();
  j["total_realized_pnl_cents"] = total_realized_pnl_cents();
  j["total_unrealized_pnl_cents"] = total_unrealized_pnl_cents();
  auto arr = nlohmann::json::array();
  for (const auto& [sym, st] : positions_) {
    if (st.quantity == 0 && st.realized_pnl_cents == 0) {
      continue;
    }
    nlohmann::json row;
    row["symbol"] = sym;
    row["quantity"] = st.quantity;
    row["avg_entry_cents"] = st.avg_entry_cents;
    row["realized_pnl_cents"] = st.realized_pnl_cents;
    row["unrealized_pnl_cents"] = unrealized_pnl_cents(sym);
    row["market_value_cents"] = market_value_cents(sym);
    row["last_mark_cents"] = st.last_mark_cents;
    arr.push_back(std::move(row));
  }
  j["positions"] = std::move(arr);
  return j;
}

}
