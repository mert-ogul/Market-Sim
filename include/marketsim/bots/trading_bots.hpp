#pragma once

#include "marketsim/domain/bot_settings.hpp"
#include "marketsim/domain/types.hpp"
#include "marketsim/matching/types.hpp"

#include <cstdint>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

namespace marketsim::bots {

/// Three simple strategies that emit market orders against house liquidity each tick.
class TradingBots {
public:
  TradingBots(domain::BotSettings settings, std::uint64_t seed);

  /// Appends @p price to the per-symbol history and may return market orders for that symbol.
  [[nodiscard]] std::vector<matching::SubmitRequest> on_price(const domain::Symbol& symbol,
                                                              domain::PriceCents price,
                                                              domain::PriceCents reference);

private:
  domain::BotSettings settings_;
  std::mt19937_64 rng_;
  std::unordered_map<domain::Symbol, std::deque<domain::PriceCents>> prices_;
};

}
