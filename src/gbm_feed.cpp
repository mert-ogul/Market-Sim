#include "marketsim/feed/gbm_feed.hpp"

#include "marketsim/feed/gbm_model.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace marketsim::feed {

namespace {

[[nodiscard]] domain::PriceCents to_price_cents(const double price) {
  if (!std::isfinite(price)) {
    return 1;
  }
  const long long rounded = std::llround(price);
  const auto cents = static_cast<domain::PriceCents>(rounded);
  return std::max<domain::PriceCents>(1, cents);
}

/// Offset so the market stream differs from the per-symbol stream for the same seed.
constexpr std::uint64_t kMarketSeedOffset = 0x9E3779B97F4A7C15ULL;
/// Offset so news draws do not disturb the symbol or market streams.
constexpr std::uint64_t kNewsSeedOffset = 0xC2B2AE3D27D4EB4FULL;

constexpr double kCompanyMoveMin = 0.05;
constexpr double kCompanyMoveMax = 0.20;
constexpr double kMarketMoveMin = 0.02;
constexpr double kMarketMoveMax = 0.07;
constexpr double kMarketDropShare = 0.8;
constexpr double kStormPerMove = 10.0;
constexpr double kStormCap = 4.0;
constexpr double kStormHalfLifeDays = 1.0;
constexpr double kTradingDaysPerYear = 252.0;

}

GbmFeed::GbmFeed(GbmFeedConfig config)
    : config_(config),
      rng_(config.seed),
      market_rng_(config.seed ^ kMarketSeedOffset),
      news_rng_(config.seed ^ kNewsSeedOffset),
      days_per_tick_(config.dt_years * kTradingDaysPerYear),
      storm_decay_(std::pow(0.5, days_per_tick_ / kStormHalfLifeDays)) {}

void GbmFeed::register_symbol(domain::Symbol symbol, GbmSymbolParams params) {
  params_[std::move(symbol)] = params;
}

void GbmFeed::register_default_symbols() {
  for (auto entry : default_symbol_table()) {
    register_symbol(std::move(entry.first), entry.second);
  }
}

bool GbmFeed::is_registered(const domain::Symbol& symbol) const {
  return params_.contains(symbol);
}

std::optional<domain::Tick> GbmFeed::next_tick(const domain::Symbol& symbol) {
  const auto pit = params_.find(symbol);
  if (pit == params_.end()) {
    return std::nullopt;
  }

  auto sit = state_.find(symbol);
  if (sit == state_.end()) {
    SymbolState initial;
    initial.price = static_cast<double>(pit->second.initial_price_cents);
    initial.cumulative_volume = 0;
    initial.next_time = 0;
    initial.storm = 1.0;
    sit = state_.emplace(symbol, initial).first;
  }

  SymbolState& st = sit->second;
  const GbmSymbolParams& p = pit->second;

  st.storm = 1.0 + (st.storm - 1.0) * storm_decay_;

  // Mixing keeps each symbol's volatility unchanged while correlating it with the market.
  const double rho = std::clamp(p.market_correlation, 0.0, 1.0);
  const double z = rho * market_shock_ + std::sqrt(1.0 - rho * rho) * normal_(rng_);
  const double factor =
      gbm_multiplicative_factor(p.drift, p.volatility * st.storm, config_.dt_years, z);
  st.price *= factor;

  double jump = market_jump_ * (0.5 + 0.5 * rho);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  if (config_.company_news_per_day > 0.0 &&
      unit(news_rng_) < config_.company_news_per_day * days_per_tick_) {
    std::uniform_real_distribution<double> size(kCompanyMoveMin, kCompanyMoveMax);
    double move = size(news_rng_);
    if (unit(news_rng_) < 0.5) {
      move = -move;
    }
    pending_news_.push_back(domain::News{.symbol = symbol, .move = move});
    jump += move;
  }

  if (jump != 0.0) {
    st.price *= 1.0 + jump;
    st.storm = std::min(kStormCap, st.storm + std::abs(jump) * kStormPerMove);
  }

  const domain::PriceCents px = to_price_cents(st.price);

  const domain::Quantity last_size = size_dist_(rng_);
  st.cumulative_volume += last_size;
  const domain::SimTime t = st.next_time;
  ++st.next_time;

  domain::Tick tick;
  tick.symbol = symbol;
  tick.last_price = px;
  tick.last_size = last_size;
  tick.cumulative_volume = st.cumulative_volume;
  tick.time = t;
  return tick;
}

void GbmFeed::begin_round() {
  market_shock_ = market_normal_(market_rng_);
  market_jump_ = 0.0;
  if (config_.market_shocks_per_day <= 0.0) {
    return;
  }
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  if (unit(news_rng_) >= config_.market_shocks_per_day * days_per_tick_) {
    return;
  }
  std::uniform_real_distribution<double> size(kMarketMoveMin, kMarketMoveMax);
  double move = size(news_rng_);
  if (unit(news_rng_) < kMarketDropShare) {
    move = -move;
  }
  market_jump_ = move;
  pending_news_.push_back(domain::News{.symbol = {}, .move = move});
}

void GbmFeed::reseed(const std::uint64_t seed) {
  rng_.seed(seed);
  market_rng_.seed(seed ^ kMarketSeedOffset);
  news_rng_.seed(seed ^ kNewsSeedOffset);
}

double GbmFeed::volatility_multiplier(const domain::Symbol& symbol) const {
  const auto it = state_.find(symbol);
  if (it == state_.end()) {
    return 1.0;
  }
  return it->second.storm;
}

std::vector<domain::News> GbmFeed::take_news() {
  return std::exchange(pending_news_, {});
}

std::vector<std::pair<domain::Symbol, GbmSymbolParams>> default_symbol_table() {
  return {
      {domain::Symbol{"AAPL"}, GbmSymbolParams{18'000, 0.10, 0.22}},
      {domain::Symbol{"GOOG"}, GbmSymbolParams{140'000, 0.09, 0.24}},
      {domain::Symbol{"MSFT"}, GbmSymbolParams{42'000, 0.11, 0.21}},
      {domain::Symbol{"TSLA"}, GbmSymbolParams{25'000, 0.15, 0.45}},
  };
}

}
