#include "marketsim/config/symbols_config.hpp"
#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/gateway/demo_seed.hpp"
#include "marketsim/gateway/register_http_api.hpp"
#include "marketsim/gateway/simulator_service.hpp"
#include "marketsim/matching/types.hpp"
#include "marketsim/runtime/simulator_runtime.hpp"

#include <gtest/gtest.h>

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

TEST(RuntimeIntegration, ApplyMarkFromTickSetsRiskReferenceForMarketBuy) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 100'000'000);

  marketsim::matching::SubmitRequest sell{.symbol = "AAPL",
                                            .side = marketsim::domain::Side::Sell,
                                            .type = marketsim::domain::OrderType::Limit,
                                            .limit_price = 10'000,
                                            .quantity = 10};
  const auto sr = svc.place_order(sell, std::nullopt);
  ASSERT_EQ(sr.first, 200);

  marketsim::domain::Tick tick{.symbol = "AAPL",
                                .last_price = 10'000,
                                .last_size = 1,
                                .cumulative_volume = 1,
                                .time = 0};
  svc.apply_mark_from_tick(tick);

  const auto buy = marketsim::matching::SubmitRequest{.symbol = "AAPL",
                                                      .side = marketsim::domain::Side::Buy,
                                                      .type = marketsim::domain::OrderType::Market,
                                                      .limit_price = std::nullopt,
                                                      .quantity = 2};
  const auto br = svc.place_order(buy, std::nullopt);
  EXPECT_EQ(br.first, 200);
  const auto pj = svc.portfolio_json();
  EXPECT_LT(pj["cash_cents"].get<std::int64_t>(), 100'000'000);
}

TEST(RuntimeIntegration, StartStopIdempotent) {
  auto cfg = marketsim::config::default_symbols_config();
  cfg.symbols.resize(1);
  cfg.pump_interval_ms = 50;

  marketsim::domain::RiskLimits limits{};
  marketsim::runtime::SimulatorRuntime runtime(limits, 1'000'000, cfg);
  runtime.start();
  runtime.start();
  runtime.stop();
  runtime.stop();
  runtime.start();
  runtime.stop();
}

TEST(RuntimeIntegration, RestTradeWhileFeedThreadsRunning) {
  auto cfg = marketsim::config::default_symbols_config();
  cfg.symbols.resize(1);
  cfg.pump_interval_ms = 8;

  marketsim::domain::RiskLimits limits{};
  marketsim::runtime::SimulatorRuntime runtime(limits, 100'000'000, cfg);
  runtime.start();

  const std::string sym = cfg.symbols[0].ticker;
  const std::int64_t mid = marketsim::config::initial_price_hint_cents(cfg, sym);
  marketsim::gateway::seed_demo_liquidity(runtime.service(), sym, mid);

  httplib::Server svr;
  marketsim::gateway::register_http_api(svr, runtime.service());

  const int port = 18355;
  std::thread th([&]() { (void)svr.listen("127.0.0.1", port); });
  for (int i = 0; i < 500; ++i) {
    if (svr.is_running()) {
      break;
    }
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_TRUE(svr.is_running());

  std::this_thread::sleep_for(40ms);

  httplib::Client cli("127.0.0.1", port);
  const std::string body = std::string(R"({"symbol":")") + sym +
                           R"(","side":"buy","type":"limit","quantity":3,"price_cents":)" +
                           std::to_string(mid + 5'000) + "}";
  const auto res = cli.Post("/api/v1/orders", body, "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  const auto j = nlohmann::json::parse(res->body);
  ASSERT_TRUE(j.at("accepted").get<bool>());

  svr.stop();
  th.join();
  runtime.stop();
}

}
