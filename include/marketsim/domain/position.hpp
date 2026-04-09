#pragma once

#include "marketsim/domain/types.hpp"

namespace marketsim::domain {

/// Long/short position with average cost for P&L (`Portfolio` owns fill-driven updates).
struct Position {
  Symbol symbol{};
  /// Positive long, negative short.
  Quantity quantity{0};
  /// Volume-weighted average cost in cents per share for the current net position.
  PriceCents average_cost_cents{0};
};

}
