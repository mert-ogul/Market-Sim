#pragma once

#include "marketsim/domain/types.hpp"

#include <optional>

namespace marketsim::domain {

/// Top-of-book quote snapshot (bid/ask with sizes).
struct Quote {
  Symbol symbol{};
  std::optional<PriceCents> bid_price{};
  std::optional<PriceCents> ask_price{};
  Quantity bid_size{0};
  Quantity ask_size{0};
  SimTime time{0};
};

/// Point-in-time last sale and cumulative volume for a symbol.
struct Tick {
  Symbol symbol{};
  PriceCents last_price{0};
  Quantity last_size{0};
  Quantity cumulative_volume{0};
  SimTime time{0};
};

}
