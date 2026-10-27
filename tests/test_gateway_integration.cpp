#include <gtest/gtest.h>

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/gateway/http_setup.hpp"
#include "marketsim/gateway/register_http_api.hpp"
#include "marketsim/gateway/simulator_service.hpp"

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

TEST(GatewayIntegration, SubmitValidOrderQuoteOrderbookCancel) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 100'000'000);

  httplib::Server svr;
  marketsim::gateway::register_http_api(svr, svc);

  const int port = 18190;
  std::thread th([&]() { svr.listen("127.0.0.1", port); });
  for (int i = 0; i < 500; ++i) {
    if (svr.is_running()) {
      break;
    }
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_TRUE(svr.is_running());

  httplib::Client cli("127.0.0.1", port);

  const auto sell = cli.Post(
      "/api/v1/orders",
      R"({"symbol":"AAPL","side":"sell","type":"limit","quantity":10,"price_cents":100})",
      "application/json");
  ASSERT_TRUE(sell);
  EXPECT_EQ(sell->status, 200);
  const auto sell_json = nlohmann::json::parse(sell->body);
  ASSERT_TRUE(sell_json.at("accepted").get<bool>());
  const auto oid = sell_json.at("order_id").get<std::uint64_t>();

  const auto quote = cli.Get("/api/v1/quote/AAPL");
  ASSERT_TRUE(quote);
  EXPECT_EQ(quote->status, 200);
  const auto qj = nlohmann::json::parse(quote->body);
  EXPECT_EQ(qj.at("symbol").get<std::string>(), "AAPL");

  const auto ob = cli.Get("/api/v1/orderbook/AAPL");
  ASSERT_TRUE(ob);
  EXPECT_EQ(ob->status, 200);
  const auto obj = nlohmann::json::parse(ob->body);
  EXPECT_TRUE(obj.at("asks").is_array());

  const auto bad = cli.Post("/api/v1/orders", "not-json", "application/json");
  ASSERT_TRUE(bad);
  EXPECT_EQ(bad->status, 400);

  const auto bad_mkt = cli.Post(
      "/api/v1/orders",
      R"({"symbol":"MSFT","side":"buy","type":"market","quantity":1})",
      "application/json");
  ASSERT_TRUE(bad_mkt);
  EXPECT_EQ(bad_mkt->status, 422);

  const auto del = cli.Delete("/api/v1/orders/" + std::to_string(oid));
  ASSERT_TRUE(del);
  EXPECT_EQ(del->status, 200);

  svr.stop();
  th.join();
}

TEST(GatewayIntegration, OpenOrdersAndSymbols) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 100'000'000, {"AAPL", "MSFT"});
  svc.apply_mark_from_tick(marketsim::domain::Tick{.symbol = "AAPL",
                                                   .last_price = 18'000,
                                                   .last_size = 1,
                                                   .cumulative_volume = 1,
                                                   .time = 0});

  httplib::Server svr;
  marketsim::gateway::register_http_api(svr, svc);

  const int port = 18193;
  std::thread th([&]() { svr.listen("127.0.0.1", port); });
  ASSERT_TRUE(marketsim::gateway::wait_until_server_running(svr));

  httplib::Client cli("127.0.0.1", port);

  const auto buy = cli.Post(
      "/api/v1/orders",
      R"({"symbol":"MSFT","side":"buy","type":"limit","quantity":7,"price_cents":40000})",
      "application/json");
  ASSERT_TRUE(buy);
  EXPECT_EQ(buy->status, 200);
  const auto buy_id = nlohmann::json::parse(buy->body).at("order_id").get<std::uint64_t>();

  const auto orders = cli.Get("/api/v1/orders");
  ASSERT_TRUE(orders);
  EXPECT_EQ(orders->status, 200);
  const auto oj = nlohmann::json::parse(orders->body);
  ASSERT_EQ(oj.at("orders").size(), 1u);
  EXPECT_EQ(oj.at("orders")[0].at("order_id").get<std::uint64_t>(), buy_id);
  EXPECT_EQ(oj.at("orders")[0].at("symbol").get<std::string>(), "MSFT");
  EXPECT_EQ(oj.at("orders")[0].at("side").get<std::string>(), "buy");
  EXPECT_EQ(oj.at("orders")[0].at("price_cents").get<std::int64_t>(), 40'000);
  EXPECT_EQ(oj.at("orders")[0].at("remaining").get<std::int64_t>(), 7);

  const auto del = cli.Delete("/api/v1/orders/" + std::to_string(buy_id));
  ASSERT_TRUE(del);
  EXPECT_EQ(del->status, 200);

  const auto empty = cli.Get("/api/v1/orders");
  ASSERT_TRUE(empty);
  EXPECT_TRUE(nlohmann::json::parse(empty->body).at("orders").empty());

  const auto symbols = cli.Get("/api/v1/symbols");
  ASSERT_TRUE(symbols);
  EXPECT_EQ(symbols->status, 200);
  const auto sj = nlohmann::json::parse(symbols->body);
  ASSERT_EQ(sj.at("symbols").size(), 2u);
  EXPECT_EQ(sj.at("symbols")[0].at("symbol").get<std::string>(), "AAPL");
  EXPECT_EQ(sj.at("symbols")[0].at("last_price_cents").get<std::int64_t>(), 18'000);
  EXPECT_EQ(sj.at("symbols")[1].at("symbol").get<std::string>(), "MSFT");
  EXPECT_TRUE(sj.at("symbols")[1].at("last_price_cents").is_null());

  const auto quote = cli.Get("/api/v1/quote/AAPL");
  ASSERT_TRUE(quote);
  EXPECT_EQ(quote->status, 200);
  EXPECT_EQ(nlohmann::json::parse(quote->body).at("last_price_cents").get<std::int64_t>(), 18'000);

  svr.stop();
  th.join();
}

TEST(GatewayIntegration, ErrorHandlerKeepsSpecificErrorBodies) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 100'000'000);

  httplib::Server svr;
  marketsim::gateway::register_http_api(svr, svc);
  marketsim::gateway::install_json_not_found_handler(svr);

  const int port = 18191;
  std::thread th([&]() { svr.listen("127.0.0.1", port); });
  ASSERT_TRUE(marketsim::gateway::wait_until_server_running(svr));

  httplib::Client cli("127.0.0.1", port);

  const auto bad = cli.Post("/api/v1/orders", "not-json", "application/json");
  ASSERT_TRUE(bad);
  EXPECT_EQ(bad->status, 400);
  EXPECT_EQ(nlohmann::json::parse(bad->body).at("error").get<std::string>(), "invalid_json");

  const auto bad_mkt = cli.Post(
      "/api/v1/orders",
      R"({"symbol":"MSFT","side":"buy","type":"market","quantity":1})",
      "application/json");
  ASSERT_TRUE(bad_mkt);
  EXPECT_EQ(bad_mkt->status, 422);
  EXPECT_EQ(nlohmann::json::parse(bad_mkt->body).at("error").get<std::string>(),
            "unknown_symbol_reference");

  const auto del = cli.Delete("/api/v1/orders/987654");
  ASSERT_TRUE(del);
  EXPECT_EQ(del->status, 404);
  EXPECT_EQ(nlohmann::json::parse(del->body).at("message").get<std::string>(),
            "order id not found or not cancellable");

  const auto missing = cli.Get("/api/v1/does-not-exist");
  ASSERT_TRUE(missing);
  EXPECT_EQ(missing->status, 404);
  EXPECT_EQ(nlohmann::json::parse(missing->body).at("error").get<std::string>(), "not_found");

  svr.stop();
  th.join();
}

TEST(GatewayIntegration, UnknownSymbolRejected) {
  marketsim::domain::RiskLimits limits{};
  marketsim::gateway::SimulatorService svc(limits, 100'000'000, {"AAPL", "MSFT"});

  const marketsim::matching::SubmitRequest appl{.symbol = "APPL",
                                                .side = marketsim::domain::Side::Buy,
                                                .type = marketsim::domain::OrderType::Limit,
                                                .limit_price = 100,
                                                .quantity = 1};
  const auto placed = svc.place_order(appl, std::nullopt);
  EXPECT_EQ(placed.first, 422);
  EXPECT_EQ(placed.second.at("error").get<std::string>(), "unknown_symbol");

  httplib::Server svr;
  marketsim::gateway::register_http_api(svr, svc);

  const int port = 18192;
  std::thread th([&]() { svr.listen("127.0.0.1", port); });
  ASSERT_TRUE(marketsim::gateway::wait_until_server_running(svr));

  httplib::Client cli("127.0.0.1", port);

  const auto quote = cli.Get("/api/v1/quote/AAPL");
  ASSERT_TRUE(quote);
  EXPECT_EQ(quote->status, 200);

  const auto ob = cli.Get("/api/v1/orderbook/APPL");
  ASSERT_TRUE(ob);
  EXPECT_EQ(ob->status, 404);
  EXPECT_EQ(nlohmann::json::parse(ob->body).at("error").get<std::string>(), "unknown_symbol");

  svr.stop();
  th.join();
}

}
