#include "marketsim/bots/trading_bots.hpp"

#include <cmath>

namespace marketsim::bots {

namespace {

// Roughly one random clip every thirty ticks at activity 1.
constexpr double kRandomChance = 0.03;
// Roughly one trend clip every fifty ticks once the window is full.
constexpr double kTrendChance = 0.02;
// Roughly one mean-reversion clip every sixty-five ticks when far from reference.
constexpr double kBargainChance = 0.015;

constexpr std::size_t kTrendWindow = 150;
constexpr double kTrendMove = 0.003;     // 0.3% from the oldest price in the window
constexpr double kBargainMove = 0.02;    // 2% from the session reference

constexpr domain::Quantity kRandomMinQty = 10;
constexpr domain::Quantity kRandomMaxQty = 200;
constexpr domain::Quantity kClipMinQty = 100;
constexpr domain::Quantity kClipMaxQty = 600;

[[nodiscard]] bool fire(std::mt19937_64& rng, const double chance, const double activity) {
  if (chance * activity <= 0.0) {
    return false;
  }
  if (chance * activity >= 1.0) {
    return true;
  }
  return std::uniform_real_distribution<double>(0.0, 1.0)(rng) < chance * activity;
}

[[nodiscard]] domain::Quantity random_qty(std::mt19937_64& rng, const domain::Quantity lo,
                                          const domain::Quantity hi) {
  return std::uniform_int_distribution<domain::Quantity>(lo, hi)(rng);
}

[[nodiscard]] matching::SubmitRequest market_order(const domain::Symbol& symbol,
                                                   const domain::Side side,
                                                   const domain::Quantity quantity,
                                                   const domain::AccountId owner) {
  return matching::SubmitRequest{.symbol = symbol,
                                 .side = side,
                                 .type = domain::OrderType::Market,
                                 .limit_price = std::nullopt,
                                 .quantity = quantity,
                                 .owner = owner};
}

}

TradingBots::TradingBots(domain::BotSettings settings, const std::uint64_t seed)
    : settings_(settings), rng_(seed) {}

std::vector<matching::SubmitRequest> TradingBots::on_price(const domain::Symbol& symbol,
                                                           const domain::PriceCents price,
                                                           const domain::PriceCents reference) {
  if (!settings_.enabled || price <= 0) {
    return {};
  }

  auto& history = prices_[symbol];
  history.push_back(price);
  if (history.size() > kTrendWindow) {
    history.pop_front();
  }

  std::vector<matching::SubmitRequest> out;

  if (fire(rng_, kRandomChance, settings_.activity)) {
    const domain::Side side =
        std::uniform_int_distribution<int>(0, 1)(rng_) == 0 ? domain::Side::Buy : domain::Side::Sell;
    out.push_back(market_order(symbol, side, random_qty(rng_, kRandomMinQty, kRandomMaxQty),
                               domain::kRandomTraderAccount));
  }

  if (history.size() >= kTrendWindow && fire(rng_, kTrendChance, settings_.activity)) {
    const domain::PriceCents oldest = history.front();
    if (oldest > 0) {
      const double move =
          static_cast<double>(price - oldest) / static_cast<double>(oldest);
      if (std::fabs(move) >= kTrendMove) {
        const domain::Side side = move > 0.0 ? domain::Side::Buy : domain::Side::Sell;
        out.push_back(market_order(symbol, side, random_qty(rng_, kClipMinQty, kClipMaxQty),
                                   domain::kTrendFollowerAccount));
      }
    }
  }

  if (reference > 0 && fire(rng_, kBargainChance, settings_.activity)) {
    const double move =
        static_cast<double>(price - reference) / static_cast<double>(reference);
    if (std::fabs(move) >= kBargainMove) {
      // Buy a faller, sell a riser.
      const domain::Side side = move < 0.0 ? domain::Side::Buy : domain::Side::Sell;
      out.push_back(market_order(symbol, side, random_qty(rng_, kClipMinQty, kClipMaxQty),
                                 domain::kBargainHunterAccount));
    }
  }

  return out;
}

}
