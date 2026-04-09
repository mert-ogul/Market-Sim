#pragma once

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

namespace marketsim::gateway {

/// JSON body for unknown routes (keeps REST clients consistent).
inline void install_json_not_found_handler(httplib::Server& server) {
  server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
    if (res.status == 0) {
      res.status = 404;
    }
    nlohmann::json j;
    j["error"] = "not_found";
    j["message"] = "no route";
    res.set_content(j.dump(), "application/json");
  });
}

/// Poll until `is_running()` or timeout. Returns whether the server thread started listening.
[[nodiscard]] inline bool wait_until_server_running(
    httplib::Server& server,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) {
  using namespace std::chrono_literals;
  const auto steps = std::max<std::chrono::milliseconds::rep>(1, timeout.count());
  for (std::chrono::milliseconds::rep i = 0; i < steps; ++i) {
    if (server.is_running()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

}
