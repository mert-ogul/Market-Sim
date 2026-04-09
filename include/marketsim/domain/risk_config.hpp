#pragma once

#include "marketsim/domain/types.hpp"

namespace marketsim::domain {

/// Static limits checked before order acceptance (enforced by `RiskManager`).
struct RiskLimits {
  /// Max absolute position size per symbol (shares).
  Quantity max_position_per_symbol{0};
  /// Max shares per single order.
  Quantity max_order_size{0};
  /// Reject orders outside [min_price_cents, max_price_cents] when set (>0).
  PriceCents min_price_cents{0};
  PriceCents max_price_cents{0};
  /// Half-width band around last reference: limit price must lie in [ref - w, ref + w]. 0 = off.
  PriceCents price_collar_cents{0};
};

/// Starting portfolio parameters for simulation runs.
struct SimulationAccount {
  PriceCents cash_cents{0};
  RiskLimits limits{};
};

}
