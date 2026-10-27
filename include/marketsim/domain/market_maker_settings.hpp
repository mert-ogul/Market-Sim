#pragma once

#include "marketsim/domain/types.hpp"

namespace marketsim::domain {

/// Simulated liquidity provider; on every feed tick it replaces its bid and ask around the tick
/// price, so the book follows the feed and resting trader orders fill when price moves through them.
struct MarketMakerSettings {
  bool enabled{false};
  std::int64_t half_spread_bps{5};  // half the quoted spread, in basis points of the tick price (at least one cent)
  Quantity quote_size{500};         // shares quoted on each side
};

}
