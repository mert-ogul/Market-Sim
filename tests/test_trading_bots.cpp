#include <gtest/gtest.h>

#include "marketsim/bots/trading_bots.hpp"
#include "marketsim/domain/bot_settings.hpp"
#include "marketsim/domain/types.hpp"

namespace {

TEST(TradingBots, DisabledBotsSendNothing) {
  marketsim::bots::TradingBots bots(marketsim::domain::BotSettings{.enabled = false, .activity = 100.0},
                                    1);
  EXPECT_TRUE(bots.on_price("AAPL", 10'000, 10'000).empty());
}

TEST(TradingBots, RandomTradersSendSmallMarketOrders) {
  marketsim::bots::TradingBots bots(marketsim::domain::BotSettings{.enabled = true, .activity = 100.0},
                                    1);
  const auto orders = bots.on_price("AAPL", 10'000, 10'000);
  ASSERT_FALSE(orders.empty());
  bool saw_random = false;
  for (const auto& order : orders) {
    if (order.owner != marketsim::domain::kRandomTraderAccount) {
      continue;
    }
    saw_random = true;
    EXPECT_EQ(order.type, marketsim::domain::OrderType::Market);
    EXPECT_GE(order.quantity, 10);
    EXPECT_LE(order.quantity, 200);
  }
  EXPECT_TRUE(saw_random);
}

TEST(TradingBots, TrendFollowersBuyARisingStock) {
  marketsim::bots::TradingBots bots(marketsim::domain::BotSettings{.enabled = true, .activity = 100.0},
                                    2);
  for (int i = 0; i < 149; ++i) {
    (void)bots.on_price("AAPL", 10'000, 10'000);
  }
  const auto orders = bots.on_price("AAPL", 10'050, 10'000);
  bool saw_trend_buy = false;
  for (const auto& order : orders) {
    if (order.owner != marketsim::domain::kTrendFollowerAccount) {
      continue;
    }
    saw_trend_buy = true;
    EXPECT_EQ(order.side, marketsim::domain::Side::Buy);
    EXPECT_EQ(order.type, marketsim::domain::OrderType::Market);
    EXPECT_GE(order.quantity, 100);
    EXPECT_LE(order.quantity, 600);
  }
  EXPECT_TRUE(saw_trend_buy);
}

TEST(TradingBots, BargainHuntersBuyAStockThatFell) {
  marketsim::bots::TradingBots bots(marketsim::domain::BotSettings{.enabled = true, .activity = 100.0},
                                    3);
  const auto buy_orders = bots.on_price("AAPL", 9'700, 10'000);
  bool saw_bargain_buy = false;
  for (const auto& order : buy_orders) {
    if (order.owner != marketsim::domain::kBargainHunterAccount) {
      continue;
    }
    saw_bargain_buy = true;
    EXPECT_EQ(order.side, marketsim::domain::Side::Buy);
    EXPECT_GE(order.quantity, 100);
    EXPECT_LE(order.quantity, 600);
  }
  EXPECT_TRUE(saw_bargain_buy);

  marketsim::bots::TradingBots rising(marketsim::domain::BotSettings{.enabled = true, .activity = 100.0},
                                      4);
  const auto sell_orders = rising.on_price("MSFT", 10'300, 10'000);
  bool saw_bargain_sell = false;
  for (const auto& order : sell_orders) {
    if (order.owner != marketsim::domain::kBargainHunterAccount) {
      continue;
    }
    saw_bargain_sell = true;
    EXPECT_EQ(order.side, marketsim::domain::Side::Sell);
    EXPECT_GE(order.quantity, 100);
    EXPECT_LE(order.quantity, 600);
  }
  EXPECT_TRUE(saw_bargain_sell);
}

}
