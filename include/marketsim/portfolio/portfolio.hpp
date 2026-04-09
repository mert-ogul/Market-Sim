#pragma once

#include "marketsim/domain/types.hpp"

#include <nlohmann/json.hpp>

#include <unordered_map>
#include <vector>

namespace marketsim::portfolio {

/// Per-symbol book state (signed quantity, entry average, realized slice, last mark).
struct PositionState {
  domain::Quantity quantity{0};
  /// Weighted-average entry for the current side (long cost or short sale price); 0 if flat.
  domain::PriceCents avg_entry_cents{0};
  /// Cumulative realized P&L attributed to this symbol (cents).
  domain::PriceCents realized_pnl_cents{0};
  /// Last mark used for unrealized P&L; defaults to avg when never marked.
  domain::PriceCents last_mark_cents{0};
};

/// Tracks cash, positions, marks, and P&L from client-side fills. All amounts are integer cents
/// except internal scaling uses int64_t for products (qty * price).
///
/// Error handling: mutating APIs return `bool` (true = applied). Invalid args (non-positive qty)
/// return false with no state change.
class Portfolio {
public:
  /// Row for read models (same inclusion rules as `to_json()` positions array).
  struct OpenPositionRow {
    domain::Symbol symbol;
    domain::Quantity quantity{};
    domain::PriceCents avg_entry_cents{};
    domain::PriceCents unrealized_pnl_cents{};
    domain::PriceCents market_value_cents{};
  };

  explicit Portfolio(domain::PriceCents initial_cash_cents = 0);

  /// Apply an execution from the client's perspective (buy = pay cash, receive stock).
  [[nodiscard]] bool apply_fill(domain::Side side, const domain::Symbol& symbol,
                                domain::PriceCents price_cents, domain::Quantity quantity);

  /// Update mark-to-market price for a symbol (does not change cash or realized P&L).
  void apply_mark(const domain::Symbol& symbol, domain::PriceCents mark_cents);

  [[nodiscard]] domain::PriceCents cash_cents() const noexcept { return cash_cents_; }

  [[nodiscard]] domain::Quantity position_quantity(const domain::Symbol& symbol) const;
  [[nodiscard]] domain::PriceCents avg_entry_cents(const domain::Symbol& symbol) const;
  [[nodiscard]] domain::PriceCents realized_pnl_cents(const domain::Symbol& symbol) const;
  [[nodiscard]] domain::PriceCents unrealized_pnl_cents(const domain::Symbol& symbol) const;
  [[nodiscard]] domain::PriceCents market_value_cents(const domain::Symbol& symbol) const;

  [[nodiscard]] domain::PriceCents total_realized_pnl_cents() const;
  [[nodiscard]] domain::PriceCents total_unrealized_pnl_cents() const;
  /// Cash plus signed mark-to-market value of all known positions.
  [[nodiscard]] domain::PriceCents equity_cents() const;

  /// JSON snapshot for REST/UI boundaries (nlohmann).
  [[nodiscard]] nlohmann::json to_json() const;

  /// Structured positions for read models (same filter as `to_json()`; no JSON round-trip).
  [[nodiscard]] std::vector<OpenPositionRow> open_position_rows() const;

private:
  [[nodiscard]] PositionState& ensure_state(const domain::Symbol& symbol);
  [[nodiscard]] const PositionState* find_state(const domain::Symbol& symbol) const;

  domain::PriceCents cash_cents_{0};
  std::unordered_map<domain::Symbol, PositionState> positions_;
};

}
