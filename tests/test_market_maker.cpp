#include <gtest/gtest.h>

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/market_maker_settings.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/gateway/simulator_service.hpp"
#include "marketsim/matching/types.hpp"

#include <nlohmann/json.hpp>

namespace {

constexpr marketsim::domain::MarketMakerSettings kMakerSettings{.enabled = true,
                                                                .half_spread_bps = 5,
                                                                .quote_size = 100};

[[nodiscard]] marketsim::domain::Tick aapl_tick(const marketsim::domain::PriceCents price) {
  return marketsim::domain::Tick{.symbol = "AAPL",
                                 .last_price = price,
                                 .last_size = 1,
                                 .cumulative_volume = 1,
                                 .time = 0};
}

TEST(MarketMaker, DisabledByDefault) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 1'000'000);
  svc.apply_mark_from_tick(aapl_tick(10'000));
  const auto q = svc.quote_json("AAPL");
  EXPECT_TRUE(q.at("bid_price_cents").is_null());
  EXPECT_TRUE(q.at("ask_price_cents").is_null());
}

TEST(MarketMaker, QuotesAroundTickPrice) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 1'000'000, {}, kMakerSettings);
  svc.apply_mark_from_tick(aapl_tick(10'000));
  const auto q = svc.quote_json("AAPL");
  EXPECT_EQ(q.at("bid_price_cents").get<std::int64_t>(), 9'995);
  EXPECT_EQ(q.at("ask_price_cents").get<std::int64_t>(), 10'005);
  EXPECT_EQ(q.at("bid_size").get<std::int64_t>(), 100);
  EXPECT_EQ(q.at("ask_size").get<std::int64_t>(), 100);
}

TEST(MarketMaker, RequoteReplacesPreviousQuotes) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 1'000'000, {}, kMakerSettings);
  svc.apply_mark_from_tick(aapl_tick(10'000));
  svc.apply_mark_from_tick(aapl_tick(10'100));
  const auto ob = svc.orderbook_json("AAPL");
  ASSERT_EQ(ob.at("bids").size(), 1u);
  ASSERT_EQ(ob.at("asks").size(), 1u);
  EXPECT_EQ(ob.at("bids")[0].at("price_cents").get<std::int64_t>(), 10'095);
  EXPECT_EQ(ob.at("asks")[0].at("price_cents").get<std::int64_t>(), 10'105);
}

TEST(MarketMaker, TraderMarketBuyFillsAgainstAsk) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 1'000'000, {}, kMakerSettings);
  svc.apply_mark_from_tick(aapl_tick(10'000));

  const marketsim::matching::SubmitRequest buy{.symbol = "AAPL",
                                               .side = marketsim::domain::Side::Buy,
                                               .type = marketsim::domain::OrderType::Market,
                                               .limit_price = std::nullopt,
                                               .quantity = 10};
  const auto br = svc.place_order(buy, std::nullopt);
  ASSERT_EQ(br.first, 200);

  const auto pj = svc.portfolio_json();
  EXPECT_EQ(pj["cash_cents"].get<std::int64_t>(), 1'000'000 - 10 * 10'005);
  ASSERT_EQ(pj["positions"].size(), 1u);
  EXPECT_EQ(pj["positions"][0]["quantity"].get<std::int64_t>(), 10);
}

TEST(MarketMaker, RestingTraderOrderFillsWhenPriceMovesThroughIt) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 1'000'000, {}, kMakerSettings);
  svc.apply_mark_from_tick(aapl_tick(10'000));

  const marketsim::matching::SubmitRequest buy{.symbol = "AAPL",
                                               .side = marketsim::domain::Side::Buy,
                                               .type = marketsim::domain::OrderType::Limit,
                                               .limit_price = 9'990,
                                               .quantity = 10};
  const auto br = svc.place_order(buy, std::nullopt);
  ASSERT_EQ(br.first, 200);
  EXPECT_EQ(br.second.at("resting_quantity").get<std::int64_t>(), 10);

  svc.apply_mark_from_tick(aapl_tick(9'980));

  const auto pj = svc.portfolio_json();
  EXPECT_EQ(pj["cash_cents"].get<std::int64_t>(), 1'000'000 - 10 * 9'990);
  ASSERT_EQ(pj["positions"].size(), 1u);
  EXPECT_EQ(pj["positions"][0]["quantity"].get<std::int64_t>(), 10);

  const auto tj = svc.trades_json();
  ASSERT_EQ(tj.at("trades").size(), 1u);
  EXPECT_EQ(tj.at("trades")[0].at("side").get<std::string>(), "buy");
  EXPECT_EQ(tj.at("trades")[0].at("aggressive_side").get<std::string>(), "sell");
}

}
