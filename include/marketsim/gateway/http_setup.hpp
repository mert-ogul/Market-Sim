#pragma once

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace marketsim::gateway {

/// JSON body for unknown routes (keeps REST clients consistent).
/// Responses that already carry a body (validation, risk and engine errors) are left untouched.
inline void install_json_not_found_handler(httplib::Server& server) {
  server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
    if (!res.body.empty()) {
      return;
    }
    if (res.status == 0) {
      res.status = 404;
    }
    nlohmann::json j;
    j["error"] = "not_found";
    j["message"] = "no route";
    res.set_content(j.dump(), "application/json");
  });
}

/// Serves the browser UI from @p dir at "/". Prints a hint and returns false when the folder is
/// missing, which usually means the binary was started outside the repository root.
inline bool mount_web_ui(httplib::Server& server, const std::string& dir = "web") {
  if (server.set_mount_point("/", dir)) {
    return true;
  }
  std::cerr << "web UI not served: folder '" << dir
            << "' not found (run from the repository root)\n";
  return false;
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
