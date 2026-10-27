#include "marketsim/config/symbols_config.hpp"
#include "marketsim/gateway/http_setup.hpp"
#include "marketsim/gateway/register_http_api.hpp"
#include "marketsim/runtime/simulator_runtime.hpp"
#include "marketsim/ui/dashboard_app.hpp"

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

  const char* host = "127.0.0.1";
  int port = 8080;
  std::filesystem::path config_path = "config/symbols.json";
  std::string ui_symbol = "AAPL";

  if (argc >= 2) {
    port = std::atoi(argv[1]);
  }
  if (argc >= 3 && argv[2][0] != '\0') {
    config_path = argv[2];
  }
  if (argc >= 4 && argv[3][0] != '\0') {
    ui_symbol = argv[3];
  }
  if (port <= 0 || port > 65535) {
    std::cerr << "invalid port\n";
    return 1;
  }

  marketsim::config::SymbolsJsonConfig cfg = marketsim::config::load_symbols_config_or_defaults(config_path);
  if (!marketsim::config::symbol_list_contains(cfg, ui_symbol)) {
    std::cerr << "UI symbol not in config: " << ui_symbol << '\n';
    return 2;
  }

  marketsim::runtime::SimulatorRuntime runtime(cfg.account.limits, cfg.account.cash_cents, cfg);
  runtime.start();

  httplib::Server svr;
  marketsim::gateway::register_http_api(svr, runtime.service());
  marketsim::gateway::install_json_not_found_handler(svr);

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  std::thread listen_thread([&]() { (void)svr.listen(host, port); });

  if (!marketsim::gateway::wait_until_server_running(svr, 500ms)) {
    g_shutdown.store(true, std::memory_order_relaxed);
    svr.stop();
    listen_thread.join();
    runtime.stop();
    return 1;
  }

  std::cout << "Integrated demo: REST http://" << host << ':' << port << " | UI symbol " << ui_symbol << '\n';
  std::cout << "Quit UI (q) or send SIGINT to exit\n";

  const int ui_rc = marketsim::ui::run_dashboard_loop(
      runtime.service(),
      marketsim::ui::DashboardAppOptions{.focus_symbol = ui_symbol, .symbols = runtime.pump_order()});

  g_shutdown.store(true, std::memory_order_relaxed);
  svr.stop();
  listen_thread.join();
  runtime.stop();
  return ui_rc;
}
