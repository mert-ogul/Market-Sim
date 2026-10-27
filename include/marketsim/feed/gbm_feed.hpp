#pragma once

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/types.hpp"

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace marketsim::feed {

/// Per-symbol GBM parameters (μ, σ annualized; starting price in cents).
struct GbmSymbolParams {
  domain::PriceCents initial_price_cents{100};
  double drift{0.0};
  double volatility{0.2};
  /// How closely the symbol follows the shared market move each round, from 0 (independent) to 1.
  double market_correlation{0.0};
};

/// Global stepping assumptions for the mock feed.
struct GbmFeedConfig {
  /// Length of one tick in years (e.g. one minute ≈ 1 / (252 * 390)).
  double dt_years{1.0 / (252.0 * 390.0)};
  /// RNG seed; same seed + same call order ⇒ identical paths (deterministic replay).
  std::uint64_t seed{42};
};

/// Mock price path generator: registers symbols, advances with `next_tick()`.
/// No global state; RNG and maps are owned by the instance.
class GbmFeed {
public:
  explicit GbmFeed(GbmFeedConfig config = {});

  void register_symbol(domain::Symbol symbol, GbmSymbolParams params);
  void register_default_symbols();

  [[nodiscard]] bool is_registered(const domain::Symbol& symbol) const;

  /// Next synthetic trade tick, or std::nullopt if the symbol is unknown.
  [[nodiscard]] std::optional<domain::Tick> next_tick(const domain::Symbol& symbol);

  /// Draws the market-wide shock shared by every symbol until the next call. Call once per round
  /// of ticks; symbols with a non-zero market_correlation then move together.
  void begin_round();

  /// Re-seeds the RNG only; does not reset prices or cumulative volume (see README note).
  void reseed(std::uint64_t seed);

  [[nodiscard]] const GbmFeedConfig& config() const noexcept { return config_; }

private:
  struct SymbolState {
    /// Unrounded path value in cents; only the published tick is rounded, so sub-cent moves accumulate.
    double price{0.0};
    domain::Quantity cumulative_volume{0};
    domain::SimTime next_time{0};
  };

  GbmFeedConfig config_;
  std::unordered_map<domain::Symbol, GbmSymbolParams> params_;
  std::unordered_map<domain::Symbol, SymbolState> state_;
  std::mt19937_64 rng_;
  std::normal_distribution<double> normal_{0.0, 1.0};
  std::uniform_int_distribution<domain::Quantity> size_dist_{1, 100};
  /// Separate stream so symbols without market correlation keep their original paths.
  std::mt19937_64 market_rng_;
  std::normal_distribution<double> market_normal_{0.0, 1.0};
  double market_shock_{0.0};
};

/// Table used by `register_default_symbols()` (AAPL, GOOG, MSFT, TSLA).
[[nodiscard]] std::vector<std::pair<domain::Symbol, GbmSymbolParams>> default_symbol_table();

}
