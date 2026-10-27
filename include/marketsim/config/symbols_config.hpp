#pragma once

#include "marketsim/domain/market_maker_settings.hpp"
#include "marketsim/domain/types.hpp"
#include "marketsim/feed/gbm_feed.hpp"

#include <filesystem>
#include <vector>

namespace marketsim::config {

/// One tradable symbol and its GBM parameters for the mock feed.
struct SymbolSpec {
  domain::Symbol ticker;
  feed::GbmSymbolParams gbm{};
};

/// Feed + symbol list loaded from JSON (or defaults).
struct SymbolsJsonConfig {
  std::vector<SymbolSpec> symbols;
  std::uint64_t feed_seed{42};
  double dt_years{1.0 / (252.0 * 390.0)};
  int pump_interval_ms{40};
  domain::MarketMakerSettings market_maker{.enabled = true};
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
