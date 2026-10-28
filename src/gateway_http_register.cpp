#include "marketsim/gateway/register_http_api.hpp"

#include "marketsim/domain/types.hpp"
#include "marketsim/gateway/order_parse.hpp"
#include "marketsim/gateway/simulator_service.hpp"

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

namespace marketsim::gateway {

void register_http_api(httplib::Server& svr, SimulatorService& svc) {
  svr.Get("/api/v1/portfolio", [&svc](const httplib::Request&, httplib::Response& res) {
    res.set_content(svc.portfolio_json().dump(), "application/json");
  });

  svr.Get("/api/v1/orders", [&svc](const httplib::Request&, httplib::Response& res) {
    res.set_content(svc.open_orders_json().dump(), "application/json");
  });

  svr.Get("/api/v1/symbols", [&svc](const httplib::Request&, httplib::Response& res) {
    res.set_content(svc.symbols_json().dump(), "application/json");
  });

  svr.Get("/api/v1/index", [&svc](const httplib::Request&, httplib::Response& res) {
    if (!svc.has_index()) {
      res.status = 404;
      res.set_content(R"({"error":"no_index","message":"no index is configured"})", "application/json");
      return;
    }
    res.set_content(svc.index_json().dump(), "application/json");
  });

  svr.Get("/api/v1/clock", [&svc](const httplib::Request&, httplib::Response& res) {
    res.set_content(svc.clock_json().dump(), "application/json");
  });

  svr.Get("/api/v1/news", [&svc](const httplib::Request&, httplib::Response& res) {
    res.set_content(svc.news_json().dump(), "application/json");
  });

  svr.Get("/api/v1/trades", [&svc](const httplib::Request&, httplib::Response& res) {
    res.set_content(svc.trades_json().dump(), "application/json");
  });

  svr.Get(R"(/api/v1/orderbook/(.+))", [&svc](const httplib::Request& req, httplib::Response& res) {
    const std::string symbol = req.matches[1];
    if (symbol.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"invalid_symbol","message":"empty symbol"})", "application/json");
      return;
    }
    if (!svc.is_tradable(symbol)) {
      res.status = 404;
      res.set_content(R"({"error":"unknown_symbol","message":"symbol is not configured"})",
                      "application/json");
      return;
    }
    res.set_content(svc.orderbook_json(symbol).dump(), "application/json");
  });

  svr.Get(R"(/api/v1/quote/(.+))", [&svc](const httplib::Request& req, httplib::Response& res) {
    const std::string symbol = req.matches[1];
    if (symbol.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"invalid_symbol","message":"empty symbol"})", "application/json");
      return;
    }
    if (!svc.is_tradable(symbol)) {
      res.status = 404;
      res.set_content(R"({"error":"unknown_symbol","message":"symbol is not configured"})",
                      "application/json");
      return;
    }
    res.set_content(svc.quote_json(symbol).dump(), "application/json");
  });

  svr.Get(R"(/api/v1/tape/(.+))", [&svc](const httplib::Request& req, httplib::Response& res) {
    const std::string symbol = req.matches[1];
    if (symbol.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"invalid_symbol","message":"empty symbol"})", "application/json");
      return;
    }
    if (!svc.is_tradable(symbol)) {
      res.status = 404;
      res.set_content(R"({"error":"unknown_symbol","message":"symbol is not configured"})",
                      "application/json");
      return;
    }
    res.set_content(svc.tape_json(symbol).dump(), "application/json");
  });

  svr.Post("/api/v1/orders", [&svc](const httplib::Request& req, httplib::Response& res) {
    const auto parsed = parse_place_order_string(req.body);
    if (!parsed.ok) {
      res.status = 400;
      nlohmann::json err;
      err["error"] = parsed.error.code;
      err["message"] = parsed.error.message;
      res.set_content(err.dump(), "application/json");
      return;
    }
    const auto [status, body] = svc.place_order(parsed.value.request, parsed.value.client_id);
    res.status = status;
    res.set_content(body.dump(), "application/json");
  });

  svr.Delete(R"(/api/v1/orders/(\d+))", [&svc](const httplib::Request& req, httplib::Response& res) {
    const std::string id_str = req.matches[1];
    const unsigned long long id_val = std::strtoull(id_str.c_str(), nullptr, 10);
    const auto [status, body] = svc.cancel_order(static_cast<marketsim::domain::OrderId>(id_val));
    res.status = status;
    res.set_content(body.dump(), "application/json");
  });
}

}
