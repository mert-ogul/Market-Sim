#include "marketsim/runtime/simulator_runtime.hpp"

#include "marketsim/domain/market_data.hpp"
#include "marketsim/domain/market_info.hpp"
#include "marketsim/feed/gbm_runner.hpp"
#include "marketsim/matching/types.hpp"

#include <chrono>
#include <optional>
#include <random>
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

/// Fresh seed per start: two device draws mixed with the clock so even a fixed device still varies.
[[nodiscard]] std::uint64_t random_seed() {
  std::random_device device;
  const auto a = static_cast<std::uint64_t>(device());
  const auto b = static_cast<std::uint64_t>(device());
  const auto t = static_cast<std::uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  return a ^ (b << 1U) ^ t;
}

}

SimulatorRuntime::SimulatorRuntime(domain::RiskLimits limits, const domain::PriceCents initial_cash_cents,
                                   config::SymbolsJsonConfig cfg)
    : cfg_(std::move(cfg)),
      seed_(cfg_.feed_seed.value_or(random_seed())),
      service_(std::move(limits), initial_cash_cents, tickers_of(cfg_), cfg_.market_maker,
               market_info_of(cfg_)),
      feed_(feed::GbmFeedConfig{.dt_years = cfg_.dt_years,
                                .seed = seed_,
                                .company_news_per_day = cfg_.company_news_per_day,
                                .market_shocks_per_day = cfg_.market_shocks_per_day}),
      clock_(cfg_.dt_years * kTradingSecondsPerYear,
             cfg_.closed_seconds * 1000 / cfg_.pump_interval_ms),
      bots_(cfg_.bots, seed_ ^ 0xD6E8FEB86659FD93ULL) {
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
    clock_.advance();
    const domain::MarketClockState state = clock_.state();
    // Clock first so the service learns about a new day before that day's ticks arrive.
    (void)ring_.try_push(eventbus::Event{state});
    (void)feed::pump_symbols<kEventRingCapacity>(feed_, pump_order_, ring_, state.open);
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
              trade_bots_on(arg.symbol);
            } else if constexpr (std::is_same_v<T, domain::MarketClockState>) {
              service_.apply_clock(arg);
            } else if constexpr (std::is_same_v<T, domain::News>) {
              service_.record_news(arg);
            }
          },
          ev);
    }
    if (!got) {
      std::this_thread::sleep_for(1ms);
    }
  }
}

void SimulatorRuntime::trade_bots_on(const domain::Symbol& symbol) {
  const std::optional<domain::PriceCents> price = service_.last_price(symbol);
  const std::optional<domain::PriceCents> reference = service_.reference_price(symbol);
  if (!price.has_value() || !reference.has_value()) {
    return;
  }
  for (const matching::SubmitRequest& request : bots_.on_price(symbol, *price, *reference)) {
    service_.submit_house_order(request);
  }
}

}
