#pragma once

#include "marketsim/bots/trading_bots.hpp"
#include "marketsim/config/symbols_config.hpp"
#include "marketsim/domain/risk_config.hpp"
#include "marketsim/domain/types.hpp"
#include "marketsim/eventbus/events.hpp"
#include "marketsim/eventbus/spsc_ring.hpp"
#include "marketsim/feed/gbm_feed.hpp"
#include "marketsim/gateway/simulator_service.hpp"
#include "marketsim/runtime/market_clock.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace marketsim::runtime {

/// Process-scoped wiring: one `SimulatorService`, one `GbmFeed`, one SPSC ring, two threads.
///
/// **Threads:** Producer advances the clock and calls `feed::pump_symbols`; consumer applies ticks,
/// clock and news, then runs simulated traders. Start/stop are not re-entrant; call from main
/// setup/teardown only.
///
/// **Sharing:** `service()` is referenced by REST and UI; all mutations remain serialized by the
/// service mutex inside `SimulatorService`.
class SimulatorRuntime {
public:
  static constexpr std::size_t kEventRingCapacity = 1024;

  explicit SimulatorRuntime(domain::RiskLimits limits, domain::PriceCents initial_cash_cents,
                            config::SymbolsJsonConfig cfg);

  SimulatorRuntime(const SimulatorRuntime&) = delete;
  SimulatorRuntime& operator=(const SimulatorRuntime&) = delete;
  SimulatorRuntime(SimulatorRuntime&&) = delete;
  SimulatorRuntime& operator=(SimulatorRuntime&&) = delete;

  ~SimulatorRuntime();

  void start();
  void stop();

  [[nodiscard]] gateway::SimulatorService& service() noexcept { return service_; }
  [[nodiscard]] const gateway::SimulatorService& service() const noexcept { return service_; }

  /// RNG seed used for this process (from config, or drawn at construction).
  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

  /// Symbols advanced each pump (same order as config).
  [[nodiscard]] const std::vector<domain::Symbol>& pump_order() const noexcept { return pump_order_; }

private:
  void producer_loop();
  void consumer_loop();
  void trade_bots_on(const domain::Symbol& symbol);

  config::SymbolsJsonConfig cfg_;
  std::uint64_t seed_;
  gateway::SimulatorService service_;
  feed::GbmFeed feed_;
  MarketClock clock_;
  bots::TradingBots bots_;
  eventbus::SpscRing<eventbus::Event, kEventRingCapacity> ring_;
  std::vector<domain::Symbol> pump_order_;

  std::mutex lifecycle_mu_;
  bool threads_started_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread producer_;
  std::thread consumer_;
};

}
