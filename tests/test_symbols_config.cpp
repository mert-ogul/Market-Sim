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
  ASSERT_TRUE(c.feed_seed.has_value());
  EXPECT_EQ(*c.feed_seed, 3u);
  EXPECT_EQ(c.pump_interval_ms, 10);
}

TEST(SymbolsConfig, SeedIsOptionalAndClockIsRead) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "marketsim_sym_seed_clock_test.json";
  {
    std::ofstream out(path);
    out << R"({"symbols":["AAPL"],"feed":{"pump_interval_ms":20},"clock":{"closed_seconds":0},"news":{"company_news_per_day":0.2,"market_shocks_per_day":-1.5}})";
  }
  const auto c = marketsim::config::load_symbols_config_or_defaults(path);
  std::filesystem::remove(path);
  EXPECT_FALSE(c.feed_seed.has_value());
  EXPECT_EQ(c.closed_seconds, 0);
  EXPECT_EQ(c.pump_interval_ms, 20);
  EXPECT_DOUBLE_EQ(c.company_news_per_day, 0.2);
  EXPECT_DOUBLE_EQ(c.market_shocks_per_day, 0.0);

  const auto defaults = marketsim::config::default_symbols_config();
  EXPECT_FALSE(defaults.feed_seed.has_value());
  EXPECT_EQ(defaults.closed_seconds, 30);
  EXPECT_DOUBLE_EQ(defaults.company_news_per_day, 0.0);
  EXPECT_DOUBLE_EQ(defaults.market_shocks_per_day, 0.0);
}

TEST(SymbolsConfig, UnknownTickerWithoutInitialPriceIsSkipped) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "marketsim_sym_skip_test.json";
  {
    std::ofstream out(path);
    out << R"({"symbols":["AAPL","NEWCO",{"symbol":"OTHER"},{"symbol":"PRICED","initial_price_cents":2500}]})";
  }
  const auto c = marketsim::config::load_symbols_config_or_defaults(path);
  std::filesystem::remove(path);
  ASSERT_EQ(c.symbols.size(), 2u);
  EXPECT_EQ(c.symbols[0].ticker, "AAPL");
  EXPECT_EQ(c.symbols[1].ticker, "PRICED");
  EXPECT_EQ(c.symbols[1].gbm.initial_price_cents, 2500);
}

TEST(SymbolsConfig, NamesCorrelationAndIndex) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "marketsim_sym_names_index_test.json";
  {
    std::ofstream out(path);
    out << R"({
      "index": {"name": "Dow 30", "divisor": 0.163},
      "symbols": [
        {"symbol": "KO", "name": "Coca-Cola Co", "initial_price_cents": 9270,
         "drift": 0.07, "volatility": 0.15, "market_correlation": 0.4},
        {"symbol": "NVDA", "initial_price_cents": 21600,
         "drift": 0.07, "volatility": 0.5, "market_correlation": 1.5}
      ]
    })";
  }
  const auto c = marketsim::config::load_symbols_config_or_defaults(path);
  std::filesystem::remove(path);
  ASSERT_EQ(c.symbols.size(), 2u);
  EXPECT_EQ(c.symbols[0].ticker, "KO");
  EXPECT_EQ(c.symbols[0].name, "Coca-Cola Co");
  EXPECT_DOUBLE_EQ(c.symbols[0].gbm.market_correlation, 0.4);
  EXPECT_EQ(c.symbols[1].ticker, "NVDA");
  EXPECT_TRUE(c.symbols[1].name.empty());
  EXPECT_DOUBLE_EQ(c.symbols[1].gbm.market_correlation, 1.0);
  EXPECT_EQ(c.index_name, "Dow 30");
  EXPECT_DOUBLE_EQ(c.index_divisor, 0.163);
}

TEST(SymbolsConfig, AccountAndMarketMakerSections) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "marketsim_sym_account_mm_test.json";
  {
    std::ofstream out(path);
    out << R"({
      "symbols":["AAPL"],
      "account":{
        "cash_cents":5000000,
        "max_order_size":1000,
        "max_position_per_symbol":2000,
        "price_collar_cents":-5
      },
      "market_maker":{"enabled":false,"half_spread_bps":12,"quote_size":250}
    })";
  }
  const auto c = marketsim::config::load_symbols_config_or_defaults(path);
  std::filesystem::remove(path);
  EXPECT_EQ(c.account.cash_cents, 5'000'000);
  EXPECT_EQ(c.account.limits.max_order_size, 1000);
  EXPECT_EQ(c.account.limits.max_position_per_symbol, 2000);
  EXPECT_EQ(c.account.limits.price_collar_cents, 0);
  EXPECT_FALSE(c.market_maker.enabled);
  EXPECT_EQ(c.market_maker.half_spread_bps, 12);
  EXPECT_EQ(c.market_maker.quote_size, 250);
}

TEST(SymbolsConfig, AccountAndMarketMakerDefaults) {
  const auto c = marketsim::config::default_symbols_config();
  EXPECT_EQ(c.account.cash_cents, 100'000'000);
  EXPECT_EQ(c.account.limits.max_order_size, 0);
  EXPECT_TRUE(c.market_maker.enabled);
  EXPECT_EQ(c.market_maker.quote_size, 500);
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
