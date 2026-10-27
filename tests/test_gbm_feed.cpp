#include <gtest/gtest.h>

#include "marketsim/eventbus/events.hpp"
#include "marketsim/eventbus/spsc_ring.hpp"
#include "marketsim/feed/gbm_feed.hpp"
#include "marketsim/feed/gbm_model.hpp"
#include "marketsim/feed/gbm_runner.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using marketsim::domain::Quantity;
using marketsim::domain::SimTime;
using marketsim::domain::Tick;
using marketsim::eventbus::Event;
using marketsim::eventbus::SpscRing;
using marketsim::feed::GbmFeed;
using marketsim::feed::GbmFeedConfig;
using marketsim::feed::GbmSymbolParams;
using marketsim::feed::default_symbol_table;
using marketsim::feed::gbm_multiplicative_factor;
using marketsim::feed::try_publish_tick;

TEST(GbmModel, MultiplicativeFactorMatchesClosedForm) {
  const double f = gbm_multiplicative_factor(0.1, 0.2, 1.0 / 252.0, 0.0);
  const double m = (0.1 - 0.5 * 0.04) * (1.0 / 252.0);
  EXPECT_NEAR(f, std::exp(m), 1e-12);
}

TEST(GbmFeed, RegisterSymbol) {
  GbmFeedConfig cfg;
  cfg.seed = 1;
  GbmFeed feed(cfg);
  feed.register_symbol("ZZZ", GbmSymbolParams{10'000, 0.0, 0.01});
  ASSERT_TRUE(feed.is_registered("ZZZ"));
  const auto t = feed.next_tick("ZZZ");
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->symbol, "ZZZ");
}

TEST(GbmFeed, DeterministicSequenceWithFixedSeed) {
  GbmFeedConfig cfg;
  cfg.seed = 12345;
  cfg.dt_years = 1.0 / (252.0 * 390.0);
  GbmFeed a(cfg);
  GbmFeed b(cfg);
  a.register_symbol("DEMO", GbmSymbolParams{50'000, 0.08, 0.25});
  b.register_symbol("DEMO", GbmSymbolParams{50'000, 0.08, 0.25});
  for (int i = 0; i < 50; ++i) {
    const auto ta = a.next_tick("DEMO");
    const auto tb = b.next_tick("DEMO");
    ASSERT_TRUE(ta.has_value());
    ASSERT_TRUE(tb.has_value());
    EXPECT_EQ(ta->last_price, tb->last_price);
    EXPECT_EQ(ta->last_size, tb->last_size);
    EXPECT_EQ(ta->cumulative_volume, tb->cumulative_volume);
    EXPECT_EQ(ta->time, tb->time);
  }
}

TEST(GbmFeed, PriceRemainsPositiveManySteps) {
  GbmFeedConfig cfg;
  cfg.seed = 999;
  cfg.dt_years = 1.0 / (252.0 * 390.0);
  GbmFeed feed(cfg);
  feed.register_symbol("VOL", GbmSymbolParams{100, 0.0, 0.8});
  for (int i = 0; i < 20'000; ++i) {
    const auto t = feed.next_tick("VOL");
    ASSERT_TRUE(t.has_value());
    EXPECT_GE(t->last_price, 1);
  }
}

TEST(GbmFeed, LowPricedSymbolKeepsMoving) {
  GbmFeedConfig cfg;
  cfg.seed = 42;
  cfg.dt_years = 2.5e-5;
  GbmFeed feed(cfg);
  feed.register_symbol("LOW", GbmSymbolParams{100, 0.0, 0.2});
  const auto first = feed.next_tick("LOW");
  ASSERT_TRUE(first.has_value());
  auto lowest = first->last_price;
  auto highest = first->last_price;
  for (int i = 1; i < 10'000; ++i) {
    const auto t = feed.next_tick("LOW");
    ASSERT_TRUE(t.has_value());
    lowest = std::min(lowest, t->last_price);
    highest = std::max(highest, t->last_price);
  }
  EXPECT_LT(lowest, highest);
}

[[nodiscard]] double pearson_log_return_correlation(const std::vector<double>& prices_a,
                                                    const std::vector<double>& prices_b) {
  const std::size_t n = prices_a.size();
  if (n < 2 || prices_b.size() != n) {
    return 0.0;
  }
  const std::size_t returns = n - 1;
  double mean_a = 0.0;
  double mean_b = 0.0;
  std::vector<double> ra(returns);
  std::vector<double> rb(returns);
  for (std::size_t i = 0; i < returns; ++i) {
    ra[i] = std::log(prices_a[i + 1] / prices_a[i]);
    rb[i] = std::log(prices_b[i + 1] / prices_b[i]);
    mean_a += ra[i];
    mean_b += rb[i];
  }
  mean_a /= static_cast<double>(returns);
  mean_b /= static_cast<double>(returns);
  double numerator = 0.0;
  double var_a = 0.0;
  double var_b = 0.0;
  for (std::size_t i = 0; i < returns; ++i) {
    const double da = ra[i] - mean_a;
    const double db = rb[i] - mean_b;
    numerator += da * db;
    var_a += da * da;
    var_b += db * db;
  }
  return numerator / std::sqrt(var_a * var_b);
}

TEST(GbmFeed, CorrelatedSymbolsMoveTogether) {
  GbmFeedConfig cfg;
  cfg.seed = 5;
  GbmFeed feed(cfg);
  feed.register_symbol("HI1", GbmSymbolParams{100'000, 0.0, 0.3, 0.95});
  feed.register_symbol("HI2", GbmSymbolParams{100'000, 0.0, 0.3, 0.95});
  feed.register_symbol("LO1", GbmSymbolParams{100'000, 0.0, 0.3, 0.0});
  feed.register_symbol("LO2", GbmSymbolParams{100'000, 0.0, 0.3, 0.0});

  std::vector<double> hi1{100'000.0};
  std::vector<double> hi2{100'000.0};
  std::vector<double> lo1{100'000.0};
  std::vector<double> lo2{100'000.0};
  for (int i = 0; i < 3'000; ++i) {
    feed.begin_round();
    const auto t_hi1 = feed.next_tick("HI1");
    const auto t_hi2 = feed.next_tick("HI2");
    const auto t_lo1 = feed.next_tick("LO1");
    const auto t_lo2 = feed.next_tick("LO2");
    ASSERT_TRUE(t_hi1.has_value());
    ASSERT_TRUE(t_hi2.has_value());
    ASSERT_TRUE(t_lo1.has_value());
    ASSERT_TRUE(t_lo2.has_value());
    hi1.push_back(static_cast<double>(t_hi1->last_price));
    hi2.push_back(static_cast<double>(t_hi2->last_price));
    lo1.push_back(static_cast<double>(t_lo1->last_price));
    lo2.push_back(static_cast<double>(t_lo2->last_price));
  }

  EXPECT_GT(pearson_log_return_correlation(hi1, hi2), 0.8);
  EXPECT_LT(std::abs(pearson_log_return_correlation(lo1, lo2)), 0.15);
}

TEST(GbmFeed, ZeroCorrelationIgnoresMarketShock) {
  GbmFeedConfig cfg;
  cfg.seed = 9;
  GbmFeed with_round(cfg);
  GbmFeed without_round(cfg);
  with_round.register_symbol("X", GbmSymbolParams{20'000, 0.05, 0.25});
  without_round.register_symbol("X", GbmSymbolParams{20'000, 0.05, 0.25});
  for (int i = 0; i < 200; ++i) {
    with_round.begin_round();
    const auto a = with_round.next_tick("X");
    const auto b = without_round.next_tick("X");
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a->last_price, b->last_price);
    EXPECT_EQ(a->last_size, b->last_size);
  }
}

TEST(GbmFeed, UnknownSymbolReturnsNullopt) {
  GbmFeed feed(GbmFeedConfig{});
  feed.register_symbol("ONLY", GbmSymbolParams{1000, 0.0, 0.1});
  EXPECT_FALSE(feed.next_tick("NOSUCH").has_value());
}

TEST(GbmFeed, TickMetadataValidity) {
  GbmFeedConfig cfg;
  cfg.seed = 7;
  GbmFeed feed(cfg);
  feed.register_symbol("META", GbmSymbolParams{12'345, 0.05, 0.15});
  Quantity prev_vol = 0;
  for (SimTime step = 0; step < 10; ++step) {
    const auto t = feed.next_tick("META");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->symbol, "META");
    EXPECT_EQ(t->time, step);
    EXPECT_GE(t->last_size, 1);
    EXPECT_LE(t->last_size, 100);
    EXPECT_GT(t->cumulative_volume, prev_vol);
    prev_vol = t->cumulative_volume;
  }
}

TEST(GbmFeed, DefaultSymbolsRegistered) {
  GbmFeed feed(GbmFeedConfig{.seed = 3});
  feed.register_default_symbols();
  const auto table = default_symbol_table();
  for (const auto& entry : table) {
    ASSERT_TRUE(feed.is_registered(entry.first)) << entry.first;
    const auto t = feed.next_tick(entry.first);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->symbol, entry.first);
  }
}

TEST(GbmRunner, TryPublishTickPushesEvent) {
  GbmFeedConfig cfg;
  cfg.seed = 11;
  GbmFeed feed(cfg);
  feed.register_symbol("PUB", GbmSymbolParams{5000, 0.0, 0.05});
  SpscRing<Event, 16> ring;
  ASSERT_TRUE(try_publish_tick(feed, "PUB", ring));
  Event ev{Tick{}};
  ASSERT_TRUE(ring.try_pop(ev));
  ASSERT_TRUE(std::holds_alternative<Tick>(ev));
  EXPECT_EQ(std::get<Tick>(ev).symbol, "PUB");
}

}
