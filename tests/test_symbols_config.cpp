#include "marketsim/config/symbols_config.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(SymbolsConfig, DefaultsContainKnownTickers) {
  const auto c = marketsim::config::default_symbols_config();
  ASSERT_FALSE(c.symbols.empty());
  EXPECT_EQ(c.symbols.front().ticker, "AAPL");
}

TEST(SymbolsConfig, MissingFileFallsBackToDefaults) {
  const auto c = marketsim::config::load_symbols_config_or_defaults("config/does_not_exist_symbols.json");
  EXPECT_FALSE(c.symbols.empty());
  EXPECT_GE(c.symbols.size(), 4u);
}

TEST(SymbolsConfig, LoadValidFileOverridesSymbols) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "marketsim_sym_test.json";
  {
    std::ofstream out(path);
    out << R"({"symbols":[{"symbol":"ZZZ","initial_price_cents":5000,"drift":0,"volatility":0.1}],"feed":{"seed":3,"pump_interval_ms":10}})";
  }
  const auto c = marketsim::config::load_symbols_config_or_defaults(path);
  std::filesystem::remove(path);
  ASSERT_EQ(c.symbols.size(), 1u);
  EXPECT_EQ(c.symbols[0].ticker, "ZZZ");
  EXPECT_EQ(c.symbols[0].gbm.initial_price_cents, 5000);
  EXPECT_EQ(c.feed_seed, 3u);
  EXPECT_EQ(c.pump_interval_ms, 10);
}

TEST(SymbolsConfig, InitialPriceHint) {
  auto c = marketsim::config::default_symbols_config();
  EXPECT_EQ(marketsim::config::initial_price_hint_cents(c, "AAPL"), 18'000);
  EXPECT_EQ(marketsim::config::initial_price_hint_cents(c, "UNKNOWN"), 18'000);
}

TEST(SymbolsConfig, SymbolListContains) {
  auto c = marketsim::config::default_symbols_config();
  EXPECT_TRUE(marketsim::config::symbol_list_contains(c, "AAPL"));
  EXPECT_FALSE(marketsim::config::symbol_list_contains(c, "NOSUCH"));
}
