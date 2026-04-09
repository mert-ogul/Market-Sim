#include "marketsim/ui/dashboard_app.hpp"

#include "marketsim/domain/types.hpp"
#include "marketsim/gateway/simulator_service.hpp"
#include "marketsim/matching/types.hpp"
#include "marketsim/ui/dashboard_ftxui.hpp"
#include "marketsim/ui/dashboard_prepare.hpp"
#include "marketsim/view/simulator_snapshot.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <atomic>
#include <charconv>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace marketsim::ui {

namespace {

[[nodiscard]] bool parse_i64(const std::string& s, std::int64_t& out) {
  if (s.empty()) {
    return false;
  }
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  const auto res = std::from_chars(begin, end, out);
  return res.ec == std::errc{} && res.ptr == end;
}

struct SnapshotStore {
  std::mutex mu;
  std::shared_ptr<const marketsim::view::SimulatorSnapshot> latest;
  std::vector<double> mid_history;
  static constexpr std::size_t kMaxMids = 256;
};

}

int run_dashboard_loop(gateway::SimulatorService& service, const DashboardAppOptions& opts) {
  using namespace std::chrono_literals;

  const std::string& symbol = opts.focus_symbol;
  const int interval_ms = opts.refresh_interval_ms < 20 ? 20 : opts.refresh_interval_ms;

  SnapshotStore store;
  auto screen = ftxui::ScreenInteractive::Fullscreen();

  std::atomic<bool> worker_stop{false};
  std::thread worker([&]() {
    while (!worker_stop.load(std::memory_order_relaxed)) {
      auto snap =
          std::make_shared<marketsim::view::SimulatorSnapshot>(service.capture_view_snapshot(symbol, 10, 14));
      {
        std::lock_guard lock(store.mu);
        store.latest = std::move(snap);
        if (store.latest->mid_price_cents.has_value()) {
          store.mid_history.push_back(static_cast<double>(*store.latest->mid_price_cents));
          if (store.mid_history.size() > SnapshotStore::kMaxMids) {
            store.mid_history.erase(store.mid_history.begin(),
                                    store.mid_history.begin() +
                                        static_cast<std::ptrdiff_t>(store.mid_history.size() - SnapshotStore::kMaxMids));
          }
        }
      }
      if (!worker_stop.load(std::memory_order_relaxed)) {
        screen.PostEvent(ftxui::Event::Custom);
      }
      for (int i = 0; i < interval_ms && !worker_stop.load(std::memory_order_relaxed); ++i) {
        std::this_thread::sleep_for(1ms);
      }
    }
  });

  std::string qty_str = "25";
  std::string px_str = "18000";
  std::string feedback = "Tab to order row | limit in cents | q quit";

  auto buy_mode = std::make_shared<bool>(true);

  auto submit = ftxui::Button("Submit LMT", [&] {
    std::int64_t qty = 0;
    std::int64_t px = 0;
    if (!parse_i64(qty_str, qty) || qty <= 0) {
      feedback = "Invalid quantity";
      return;
    }
    if (!parse_i64(px_str, px) || px <= 0) {
      feedback = "Invalid price (cents)";
      return;
    }
    matching::SubmitRequest req{.symbol = symbol,
                                .side = *buy_mode ? domain::Side::Buy : domain::Side::Sell,
                                .type = domain::OrderType::Limit,
                                .limit_price = px,
                                .quantity = qty};
    const auto [code, body] = service.place_order(req, std::nullopt);
    if (code == 200) {
      feedback = "Order accepted id=" + std::to_string(body["order_id"].get<std::uint64_t>());
    } else {
      feedback = body.dump();
    }
  });

  auto buy_btn = ftxui::Button("Buy", [buy_mode] { *buy_mode = true; });
  auto sell_btn = ftxui::Button("Sell", [buy_mode] { *buy_mode = false; });

  const auto qty_in = ftxui::Input(&qty_str, "qty");
  const auto px_in = ftxui::Input(&px_str, "px¢");

  auto order_row = ftxui::Container::Horizontal({
      buy_btn,
      sell_btn,
      qty_in,
      px_in,
      submit,
  });

  auto dashboard = ftxui::Renderer([&] {
    std::shared_ptr<const marketsim::view::SimulatorSnapshot> snap;
    std::vector<double> mids;
    {
      std::lock_guard lock(store.mu);
      snap = store.latest;
      mids = store.mid_history;
    }
    const int tw = screen.dimx();
    const int th = screen.dimy();

    if (!snap) {
      return ftxui::vbox(
          {ftxui::text("Connecting to simulator…") | ftxui::hcenter | ftxui::flex, ftxui::filler()});
    }

    DashboardRenderModel model = prepare_dashboard_render(*snap, mids);
    const ftxui::Element grid = render_dashboard_grid(model, tw, th);
    const ftxui::Element foot = ftxui::hbox({
        ftxui::text(" " + feedback + " ") | ftxui::dim,
    }) |
                                ftxui::border;
    return ftxui::vbox({grid | ftxui::flex, foot});
  });

  auto root = ftxui::Container::Vertical({dashboard, order_row});

  auto wrapped = ftxui::CatchEvent(root, [&](const ftxui::Event& e) {
    if (e == ftxui::Event::Character('q')) {
      worker_stop.store(true, std::memory_order_relaxed);
      screen.Exit();
      return true;
    }
    return false;
  });

  screen.Loop(wrapped);
  worker_stop.store(true, std::memory_order_relaxed);
  if (worker.joinable()) {
    worker.join();
  }
  return 0;
}

}
