#pragma once

#include "marketsim/domain/market_maker_settings.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/domain/types.hpp"
#include "marketsim/feed/gbm_feed.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace marketsim::config {

/// One tradable symbol and its GBM parameters for the mock feed.
struct SymbolSpec {
  domain::Symbol ticker;
  feed::GbmSymbolParams gbm{};
  /// Company name for display; empty when the config gives none.
  std::string name{};
};

/// Feed, symbol list, trader account and market maker settings.
struct SymbolsJsonConfig {
  std::vector<SymbolSpec> symbols;
  std::uint64_t feed_seed{42};
  double dt_years{1.0 / (252.0 * 390.0)};
  int pump_interval_ms{40};
  domain::MarketMakerSettings market_maker{.enabled = true};
  /// Starting cash and pre-trade limits for the trader (limits of 0 are disabled).
  domain::SimulationAccount account{.cash_cents = 100'000'000};
  /// Price-weighted index over all symbols; a divisor of 0 disables it.
  std::string index_name;
  double index_divisor{0.0};
};

/// Built-in defaults: same symbols as `GbmFeed::register_default_symbols()` with table params.
[[nodiscard]] SymbolsJsonConfig default_symbols_config();

/// Load `config/symbols.json`-style file. On missing file or parse error, returns defaults().
[[nodiscard]] SymbolsJsonConfig load_symbols_config_or_defaults(const std::filesystem::path& path);

/// Best-effort mid for seeding liquidity (GBM initial price hint from config).
[[nodiscard]] domain::PriceCents initial_price_hint_cents(const SymbolsJsonConfig& cfg,
                                                          const domain::Symbol& symbol);

[[nodiscard]] bool symbol_list_contains(const SymbolsJsonConfig& cfg, const domain::Symbol& symbol);

}
