#include "marketsim/feed/gbm_feed.hpp"

#include "marketsim/feed/gbm_model.hpp"

#include <algorithm>
#include <cmath>

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

}

GbmFeed::GbmFeed(GbmFeedConfig config)
    : config_(config), rng_(config.seed) {}

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
    sit = state_.emplace(symbol, initial).first;
  }

  SymbolState& st = sit->second;
  const GbmSymbolParams& p = pit->second;

  const double z = normal_(rng_);
  const double factor =
      gbm_multiplicative_factor(p.drift, p.volatility, config_.dt_years, z);
  st.price *= factor;
  const domain::PriceCents px = to_price_cents(st.price);
  st.price = static_cast<double>(px);

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

void GbmFeed::reseed(const std::uint64_t seed) {
  rng_.seed(seed);
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
