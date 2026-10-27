#include "marketsim/config/symbols_config.hpp"

#include "marketsim/feed/gbm_feed.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

namespace marketsim::config {

namespace {

void warn_missing_initial_price(const std::string& ticker) {
  std::cerr << "symbols config: skipping " << ticker
            << " (not a built-in symbol, so initial_price_cents is required)\n";
}

[[nodiscard]] std::unordered_map<domain::Symbol, feed::GbmSymbolParams> default_params_map() {
  std::unordered_map<domain::Symbol, feed::GbmSymbolParams> m;
  for (auto& [sym, p] : feed::default_symbol_table()) {
    m.emplace(std::move(sym), std::move(p));
  }
  return m;
}

[[nodiscard]] feed::GbmSymbolParams params_for_ticker(const std::string& ticker,
                                                      const std::unordered_map<domain::Symbol, feed::GbmSymbolParams>& defs) {
  const auto it = defs.find(ticker);
  if (it != defs.end()) {
    return it->second;
  }
  return feed::GbmSymbolParams{};
}

void parse_symbol_element(const nlohmann::json& el, const std::unordered_map<domain::Symbol, feed::GbmSymbolParams>& defs,
                          std::vector<SymbolSpec>& out) {
  if (el.is_string()) {
    const std::string t = el.get<std::string>();
    if (!t.empty()) {
      if (!defs.contains(t)) {
        warn_missing_initial_price(t);
        return;
      }
      out.push_back(SymbolSpec{.ticker = t, .gbm = params_for_ticker(t, defs)});
    }
    return;
  }
  if (!el.is_object()) {
    return;
  }
  if (!el.contains("symbol") || !el["symbol"].is_string()) {
    return;
  }
  SymbolSpec s;
  s.ticker = el["symbol"].get<std::string>();
  s.gbm = params_for_ticker(s.ticker, defs);
  bool has_positive_price = false;
  if (el.contains("initial_price_cents") && el["initial_price_cents"].is_number_integer()) {
    const auto px = el["initial_price_cents"].get<domain::PriceCents>();
    if (px > 0) {
      s.gbm.initial_price_cents = px;
      has_positive_price = true;
    }
  }
  if (el.contains("drift") && el["drift"].is_number()) {
    s.gbm.drift = el["drift"].get<double>();
  }
  if (el.contains("volatility") && el["volatility"].is_number()) {
    s.gbm.volatility = el["volatility"].get<double>();
  }
  if (s.ticker.empty()) {
    return;
  }
  if (!defs.contains(s.ticker) && !has_positive_price) {
    warn_missing_initial_price(s.ticker);
    return;
  }
  out.push_back(std::move(s));
}

}

SymbolsJsonConfig default_symbols_config() {
  SymbolsJsonConfig c;
  for (const auto& [sym, p] : feed::default_symbol_table()) {
    c.symbols.push_back(SymbolSpec{.ticker = sym, .gbm = p});
  }
  c.feed_seed = 42;
  c.dt_years = feed::GbmFeedConfig{}.dt_years;
  c.pump_interval_ms = 40;
  return c;
}

SymbolsJsonConfig load_symbols_config_or_defaults(const std::filesystem::path& path) {
  SymbolsJsonConfig fallback = default_symbols_config();

  std::ifstream in(path);
  if (!in) {
    return fallback;
  }

  nlohmann::json j;
  try {
    in >> j;
  } catch (...) {
    return fallback;
  }

  if (!j.is_object()) {
    return fallback;
  }

  const auto defs = default_params_map();
  SymbolsJsonConfig out;
  out.feed_seed = fallback.feed_seed;
  out.dt_years = fallback.dt_years;
  out.pump_interval_ms = fallback.pump_interval_ms;

  if (j.contains("feed") && j["feed"].is_object()) {
    const auto& f = j["feed"];
    if (f.contains("seed") && f["seed"].is_number_unsigned()) {
      out.feed_seed = f["seed"].get<std::uint64_t>();
    }
    if (f.contains("dt_years") && f["dt_years"].is_number()) {
      out.dt_years = f["dt_years"].get<double>();
    }
    if (f.contains("pump_interval_ms") && f["pump_interval_ms"].is_number_integer()) {
      out.pump_interval_ms = static_cast<int>(f["pump_interval_ms"].get<std::int64_t>());
    }
  }
  if (out.pump_interval_ms < 5) {
    out.pump_interval_ms = 5;
  }
  if (out.pump_interval_ms > 60'000) {
    out.pump_interval_ms = 60'000;
  }

  if (j.contains("symbols") && j["symbols"].is_array()) {
    for (const auto& el : j["symbols"]) {
      parse_symbol_element(el, defs, out.symbols);
    }
  }

  if (out.symbols.empty()) {
    return fallback;
  }
  return out;
}

domain::PriceCents initial_price_hint_cents(const SymbolsJsonConfig& cfg, const domain::Symbol& symbol) {
  for (const SymbolSpec& s : cfg.symbols) {
    if (s.ticker == symbol) {
      return s.gbm.initial_price_cents;
    }
  }
  return 18'000;
}

bool symbol_list_contains(const SymbolsJsonConfig& cfg, const domain::Symbol& symbol) {
  for (const SymbolSpec& s : cfg.symbols) {
    if (s.ticker == symbol) {
      return true;
    }
  }
  return false;
}

}
