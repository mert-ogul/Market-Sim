#include <gtest/gtest.h>

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/market_maker_settings.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/domain/types.hpp"
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

TEST(MarketMaker, QuotesSeveralLevelsWithGrowingSize) {
  constexpr marketsim::domain::MarketMakerSettings settings{.enabled = true,
                                                            .half_spread_bps = 5,
                                                            .quote_size = 100,
                                                            .levels = 3};
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 1'000'000, {}, settings);
  svc.apply_mark_from_tick(aapl_tick(10'000));
  const auto ob = svc.orderbook_json("AAPL");
  ASSERT_EQ(ob.at("bids").size(), 3u);
  ASSERT_EQ(ob.at("asks").size(), 3u);
  EXPECT_EQ(ob.at("bids")[2].at("price_cents").get<std::int64_t>(), 9'985);
  EXPECT_EQ(ob.at("bids")[2].at("total_quantity").get<std::int64_t>(), 300);
  EXPECT_EQ(ob.at("asks")[1].at("price_cents").get<std::int64_t>(), 10'010);
  EXPECT_EQ(ob.at("asks")[1].at("total_quantity").get<std::int64_t>(), 200);
}

TEST(MarketMaker, BigOrderWalksTheBookAndPaysMore) {
  constexpr marketsim::domain::MarketMakerSettings settings{.enabled = true,
                                                            .half_spread_bps = 5,
                                                            .quote_size = 100,
                                                            .levels = 3};
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 10'000'000, {}, settings);
  svc.apply_mark_from_tick(aapl_tick(10'000));

  const marketsim::matching::SubmitRequest buy{.symbol = "AAPL",
                                               .side = marketsim::domain::Side::Buy,
                                               .type = marketsim::domain::OrderType::Market,
                                               .limit_price = std::nullopt,
                                               .quantity = 250};
  const auto br = svc.place_order(buy, std::nullopt);
  ASSERT_EQ(br.first, 200);
  ASSERT_EQ(br.second.at("trades").size(), 2u);
  EXPECT_EQ(br.second.at("trades")[0].at("price_cents").get<std::int64_t>(), 10'005);
  EXPECT_EQ(br.second.at("trades")[0].at("quantity").get<std::int64_t>(), 100);
  EXPECT_EQ(br.second.at("trades")[1].at("price_cents").get<std::int64_t>(), 10'010);
  EXPECT_EQ(br.second.at("trades")[1].at("quantity").get<std::int64_t>(), 150);
}

TEST(MarketMaker, BuyingPushesThePriceUp) {
  constexpr marketsim::domain::MarketMakerSettings settings{.enabled = true,
                                                            .half_spread_bps = 5,
                                                            .quote_size = 1'000,
                                                            .levels = 1,
                                                            .impact_bps_per_1000_shares = 10.0};
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 20'000'000, {}, settings);
  svc.apply_mark_from_tick(aapl_tick(10'000));

  const marketsim::matching::SubmitRequest buy{.symbol = "AAPL",
                                               .side = marketsim::domain::Side::Buy,
                                               .type = marketsim::domain::OrderType::Market,
                                               .limit_price = std::nullopt,
                                               .quantity = 1'000};
  ASSERT_EQ(svc.place_order(buy, std::nullopt).first, 200);

  svc.apply_mark_from_tick(aapl_tick(10'000));
  EXPECT_EQ(svc.last_price("AAPL"), 10'010);
}

TEST(MarketMaker, HouseOrdersFillTheTapeButNotYourTrades) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 1'000'000, {"AAPL"}, kMakerSettings);
  svc.apply_mark_from_tick(aapl_tick(10'000));

  const marketsim::matching::SubmitRequest sell{.symbol = "AAPL",
                                                .side = marketsim::domain::Side::Sell,
                                                .type = marketsim::domain::OrderType::Market,
                                                .limit_price = std::nullopt,
                                                .quantity = 25,
                                                .owner = marketsim::domain::kRandomTraderAccount};
  svc.submit_house_order(sell);

  const auto tape = svc.tape_json("AAPL");
  ASSERT_EQ(tape.at("trades").size(), 1u);
  EXPECT_EQ(tape.at("trades")[0].at("quantity").get<std::int64_t>(), 25);
  EXPECT_EQ(tape.at("trades")[0].at("aggressive_side").get<std::string>(), "sell");

  const auto symbols = svc.symbols_json();
  ASSERT_EQ(symbols.at("symbols").size(), 1u);
  EXPECT_EQ(symbols.at("symbols")[0].at("volume").get<std::int64_t>(), 25);

  EXPECT_TRUE(svc.trades_json().at("trades").empty());
  EXPECT_EQ(svc.portfolio_json().at("cash_cents").get<std::int64_t>(), 1'000'000);

  svc.apply_clock(
      marketsim::domain::MarketClockState{.day = 1, .minute_of_day = 960, .open = false});
  svc.submit_house_order(sell);
  EXPECT_EQ(svc.tape_json("AAPL").at("trades").size(), 1u);
  EXPECT_EQ(svc.symbols_json().at("symbols")[0].at("volume").get<std::int64_t>(), 25);
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
