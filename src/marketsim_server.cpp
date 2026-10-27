#include "marketsim/config/symbols_config.hpp"
#include "marketsim/gateway/http_setup.hpp"
#include "marketsim/gateway/register_http_api.hpp"
#include "marketsim/runtime/simulator_runtime.hpp"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_shutdown{false};

extern "C" void handle_signal(int) { g_shutdown.store(true, std::memory_order_relaxed); }

}

int main(int argc, char* argv[]) {
  using namespace std::chrono_literals;

  const char* host = "0.0.0.0";
  int port = 8080;
  std::filesystem::path config_path = "config/symbols.json";

  if (argc >= 2) {
    port = std::atoi(argv[1]);
  }
  if (argc >= 3 && argv[2][0] != '\0') {
    config_path = argv[2];
  }
  if (port <= 0 || port > 65535) {
    std::cerr << "invalid port\n";
    return 1;
  }

  marketsim::config::SymbolsJsonConfig cfg = marketsim::config::load_symbols_config_or_defaults(config_path);
  marketsim::runtime::SimulatorRuntime runtime(cfg.account.limits, cfg.account.cash_cents, cfg);
  runtime.start();

  httplib::Server svr;
  marketsim::gateway::register_http_api(svr, runtime.service());
  marketsim::gateway::install_json_not_found_handler(svr);
  const bool web_ui = marketsim::gateway::mount_web_ui(svr);

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  std::thread listen_thread([&]() {
    if (!svr.listen(host, port)) {
      std::cerr << "listen failed\n";
    }
  });

  if (!marketsim::gateway::wait_until_server_running(svr, 500ms)) {
    g_shutdown.store(true, std::memory_order_relaxed);
    svr.stop();
    listen_thread.join();
    runtime.stop();
    return 1;
  }

  std::cout << "marketsim REST listening on http://" << host << ':' << port << '\n';
  if (web_ui) {
    std::cout << "web UI: http://localhost:" << port << '\n';
  }
  std::cout << "config: " << config_path.string() << '\n';
  std::cout << "Ctrl+C to stop\n";

  while (svr.is_running() && !g_shutdown.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(100ms);
  }

  svr.stop();
  listen_thread.join();
  runtime.stop();
  return 0;
}
