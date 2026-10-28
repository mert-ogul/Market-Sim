#pragma once

#include "marketsim/domain/types.hpp"
#include "marketsim/eventbus/events.hpp"
#include "marketsim/eventbus/spsc_ring.hpp"
#include "marketsim/feed/gbm_feed.hpp"

#include <cstddef>
#include <vector>

namespace marketsim::feed {

/// Pushes one tick for @p symbol onto the ring. Returns false if the symbol is unknown or
/// `try_push` fails (full buffer). Caller can use `feed.is_registered` to disambiguate.
template <std::size_t Capacity>
[[nodiscard]] bool try_publish_tick(GbmFeed& feed, const domain::Symbol& symbol,
                                    eventbus::SpscRing<eventbus::Event, Capacity>& ring) {
  std::optional<domain::Tick> tick = feed.next_tick(symbol);
  if (!tick.has_value()) {
    return false;
  }
  return ring.try_push(eventbus::Event{std::move(*tick)});
}

/// Advances each listed symbol once, in order. Returns how many ticks were published.
/// When @p publish_ticks is false the path still moves but nothing is pushed (overnight gap).
template <std::size_t Capacity>
[[nodiscard]] std::size_t pump_symbols(GbmFeed& feed,
                                       const std::vector<domain::Symbol>& symbols,
                                       eventbus::SpscRing<eventbus::Event, Capacity>& ring,
                                       bool publish_ticks = true) {
  std::size_t n = 0;
  feed.begin_round();
  for (const domain::Symbol& sym : symbols) {
    if (!publish_ticks) {
      (void)feed.next_tick(sym);
      continue;
    }
    if (try_publish_tick(feed, sym, ring)) {
      ++n;
    }
  }
  return n;
}

}
