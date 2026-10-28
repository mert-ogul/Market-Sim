#include "marketsim/runtime/market_clock.hpp"

#include <gtest/gtest.h>

TEST(MarketClock, StartsAtTheOpenOfDayOne) {
  const marketsim::runtime::MarketClock clock(60.0, 3);
  const marketsim::domain::MarketClockState s = clock.state();
  EXPECT_EQ(s.day, 1);
  EXPECT_EQ(s.minute_of_day, 570);
  EXPECT_TRUE(s.open);
}

TEST(MarketClock, ClosesAfterTheSessionAndOpensTheNextDay) {
  marketsim::runtime::MarketClock clock(60.0, 3);
  for (int i = 0; i < 389; ++i) {
    clock.advance();
  }
  marketsim::domain::MarketClockState s = clock.state();
  EXPECT_TRUE(s.open);
  EXPECT_EQ(s.day, 1);
  EXPECT_EQ(s.minute_of_day, 959);

  clock.advance();
  s = clock.state();
  EXPECT_FALSE(s.open);
  EXPECT_EQ(s.day, 1);
  EXPECT_EQ(s.minute_of_day, 960);

  clock.advance();
  clock.advance();
  clock.advance();
  s = clock.state();
  EXPECT_TRUE(s.open);
  EXPECT_EQ(s.day, 2);
  EXPECT_EQ(s.minute_of_day, 570);
}
