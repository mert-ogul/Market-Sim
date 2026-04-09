#pragma once

#include <string>

namespace marketsim::gateway {
class SimulatorService;
}

namespace marketsim::ui {

struct DashboardAppOptions {
  std::string focus_symbol{"AAPL"};
  int refresh_interval_ms{120};
};

/// Blocks until the user quits (e.g. `q`). Returns process exit code (0 ok, non-zero error).
[[nodiscard]] int run_dashboard_loop(marketsim::gateway::SimulatorService& svc, const DashboardAppOptions& opts);

}
