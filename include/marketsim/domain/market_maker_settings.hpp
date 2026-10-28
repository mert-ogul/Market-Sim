#pragma once

#include "marketsim/domain/types.hpp"

namespace marketsim::domain {

/// Simulated liquidity provider; on every feed tick it replaces its quotes around the tick price,
/// so the book follows the feed and resting trader orders fill when price moves through them.
/// Level k sits k half-spreads from the price and holds k times quote_size.
struct MarketMakerSettings {
  bool enabled{false};
  std::int64_t half_spread_bps{5};  // half the quoted spread, in basis points of the tick price (at least one cent)
  Quantity quote_size{500};         // shares quoted on each side at level 1
  std::int64_t levels{1};
  /// Log price shift per 1,000 shares traded against the maker (buy positive, sell negative).
  double impact_bps_per_1000_shares{0.0};
};

}
