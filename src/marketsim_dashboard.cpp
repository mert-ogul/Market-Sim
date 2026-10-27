#include "marketsim/config/symbols_config.hpp"
#include "marketsim/domain/types.hpp"
#include "marketsim/runtime/simulator_runtime.hpp"
#include "marketsim/ui/dashboard_app.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  marketsim::domain::Symbol symbol = "AAPL";
  std::filesystem::path config_path = "config/symbols.json";

  if (argc >= 2 && argv[1][0] != '\0') {
    symbol = argv[1];
  }
  if (argc >= 3 && argv[2][0] != '\0') {
    config_path = argv[2];
  }

  marketsim::config::SymbolsJsonConfig cfg = marketsim::config::load_symbols_config_or_defaults(config_path);
  if (!marketsim::config::symbol_list_contains(cfg, symbol)) {
    std::cerr << "symbol not in config: " << symbol << '\n';
    return 2;
  }

  marketsim::runtime::SimulatorRuntime runtime(cfg.account.limits, cfg.account.cash_cents, cfg);
  runtime.start();

  const int rc = marketsim::ui::run_dashboard_loop(
      runtime.service(),
      marketsim::ui::DashboardAppOptions{.focus_symbol = symbol, .symbols = runtime.pump_order()});
  runtime.stop();
  return rc;
}
