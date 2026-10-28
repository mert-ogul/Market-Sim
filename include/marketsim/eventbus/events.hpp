#pragma once

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/trade.hpp"
#include "marketsim/domain/types.hpp"

#include <variant>

namespace marketsim::eventbus {

/// New order accepted by the gateway / engine (boundary event).
struct OrderNew {
  domain::Symbol symbol{};
  domain::OrderId order_id{0};
  domain::Side side{domain::Side::Buy};
  domain::OrderType order_type{domain::OrderType::Limit};
  domain::SimTime time{0};
};

/// Cancel request applied to a resting order.
struct OrderCancel {
  domain::OrderId order_id{0};
  domain::Symbol symbol{};
  domain::SimTime time{0};
};

/// Portfolio snapshot delta for UI / risk (minimal fields; extend later).
struct PortfolioUpdate {
  domain::SimTime time{0};
  domain::PriceCents equity_cents{0};
  domain::PriceCents cash_cents{0};
};

/// Type-safe union of feed and engine events for the SPSC bus.
/// `std::variant` gives tagged dispatch, value semantics, and clear destruction rules
/// without manual union bookkeeping; alternatives are inline in the ring buffer (no heap
/// per event). The largest alternative governs `sizeof(Event)`; keep payloads small on hot paths.
using Event = std::variant<domain::Tick, OrderNew, OrderCancel, domain::Trade, domain::Quote,
                             PortfolioUpdate, domain::MarketClockState>;

}
