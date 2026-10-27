#include "marketsim/runtime/simulator_runtime.hpp"

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/market_info.hpp"
#include "marketsim/feed/gbm_runner.hpp"

#include <chrono>
#include <type_traits>
#include <variant>

namespace marketsim::runtime {

namespace {

std::vector<domain::Symbol> tickers_of(const config::SymbolsJsonConfig& cfg) {
  std::vector<domain::Symbol> out;
  out.reserve(cfg.symbols.size());
  for (const config::SymbolSpec& s : cfg.symbols) {
    out.push_back(s.ticker);
  }
  return out;
}

domain::MarketInfo market_info_of(const config::SymbolsJsonConfig& cfg) {
  domain::MarketInfo info;
  for (const config::SymbolSpec& s : cfg.symbols) {
    if (!s.name.empty()) {
      info.company_names.emplace(s.ticker, s.name);
    }
  }
  info.index_name = cfg.index_name;
  info.index_divisor = cfg.index_divisor;
  return info;
}

}

SimulatorRuntime::SimulatorRuntime(domain::RiskLimits limits, const domain::PriceCents initial_cash_cents,
                                   config::SymbolsJsonConfig cfg)
    : cfg_(std::move(cfg)),
      service_(std::move(limits), initial_cash_cents, tickers_of(cfg_), cfg_.market_maker,
               market_info_of(cfg_)),
      feed_(feed::GbmFeedConfig{.dt_years = cfg_.dt_years, .seed = cfg_.feed_seed}) {
  for (const config::SymbolSpec& s : cfg_.symbols) {
    feed_.register_symbol(s.ticker, s.gbm);
    pump_order_.push_back(s.ticker);
  }
}

SimulatorRuntime::~SimulatorRuntime() { stop(); }

void SimulatorRuntime::start() {
  std::lock_guard lock(lifecycle_mu_);
  if (threads_started_) {
    return;
  }
  stop_requested_.store(false, std::memory_order_relaxed);
  producer_ = std::thread([this] { producer_loop(); });
  consumer_ = std::thread([this] { consumer_loop(); });
  threads_started_ = true;
}

void SimulatorRuntime::stop() {
  std::lock_guard lock(lifecycle_mu_);
  if (!threads_started_) {
    return;
  }
  stop_requested_.store(true, std::memory_order_relaxed);
  if (producer_.joinable()) {
    producer_.join();
  }
  if (consumer_.joinable()) {
    consumer_.join();
  }
  threads_started_ = false;
}

void SimulatorRuntime::producer_loop() {
  using namespace std::chrono_literals;
  while (!stop_requested_.load(std::memory_order_relaxed)) {
    (void)feed::pump_symbols<kEventRingCapacity>(feed_, pump_order_, ring_);
    const int ms = cfg_.pump_interval_ms;
    for (int i = 0; i < ms && !stop_requested_.load(std::memory_order_relaxed); ++i) {
      std::this_thread::sleep_for(1ms);
    }
  }
}

void SimulatorRuntime::consumer_loop() {
  using namespace std::chrono_literals;
  eventbus::Event ev;
  while (!stop_requested_.load(std::memory_order_relaxed)) {
    bool got = false;
    while (ring_.try_pop(ev)) {
      got = true;
      std::visit(
          [this](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, domain::Tick>) {
              service_.apply_mark_from_tick(arg);
            }
          },
          ev);
    }
    if (!got) {
      std::this_thread::sleep_for(1ms);
    }
  }
}

}
