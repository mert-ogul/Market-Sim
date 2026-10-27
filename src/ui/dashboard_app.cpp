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
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
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

[[nodiscard]] std::string format_api_error(const nlohmann::json& body) {
  if (body.contains("error") && body.contains("message") && body["error"].is_string() &&
      body["message"].is_string()) {
    return body["error"].get<std::string>() + ": " + body["message"].get<std::string>();
  }
  return body.dump();
}

struct SnapshotStore {
  std::mutex mu;
  std::string symbol;
  std::shared_ptr<const marketsim::view::SimulatorSnapshot> latest;
  std::vector<double> mid_history;
  static constexpr std::size_t kMaxMids = 256;
};

}

int run_dashboard_loop(gateway::SimulatorService& service, const DashboardAppOptions& opts) {
  using namespace std::chrono_literals;
  using namespace ftxui;

  const int interval_ms = opts.refresh_interval_ms < 20 ? 20 : opts.refresh_interval_ms;

  SnapshotStore store;
  store.symbol = opts.focus_symbol;
  auto screen = ScreenInteractive::Fullscreen();

  const auto current_symbol = [&]() {
    std::lock_guard lock(store.mu);
    return store.symbol;
  };

  std::atomic<bool> worker_stop{false};
  std::thread worker([&]() {
    while (!worker_stop.load(std::memory_order_relaxed)) {
      const std::string symbol = current_symbol();
      auto snap =
          std::make_shared<marketsim::view::SimulatorSnapshot>(service.capture_view_snapshot(symbol, 10, 14));
      {
        std::lock_guard lock(store.mu);
        if (store.symbol != symbol) {
          // Drop snapshot captured for a symbol that is no longer selected.
        } else {
          store.latest = std::move(snap);
          if (store.latest->mid_price_cents.has_value()) {
            store.mid_history.push_back(static_cast<double>(*store.latest->mid_price_cents));
            if (store.mid_history.size() > SnapshotStore::kMaxMids) {
              store.mid_history.erase(
                  store.mid_history.begin(),
                  store.mid_history.begin() +
                      static_cast<std::ptrdiff_t>(store.mid_history.size() - SnapshotStore::kMaxMids));
            }
          }
        }
      }
      if (!worker_stop.load(std::memory_order_relaxed)) {
        screen.PostEvent(Event::Custom);
      }
      for (int i = 0; i < interval_ms && !worker_stop.load(std::memory_order_relaxed); ++i) {
        std::this_thread::sleep_for(1ms);
      }
    }
  });

  std::string qty_str = "25";
  std::string px_str = "18000";
  std::string cancel_id_str;
  std::string feedback =
      "Tab between controls | prices in cents | [ ] switch symbol | o overview | q quit";

  // The overview opens first whenever there is more than one symbol to choose from.
  bool show_overview = opts.symbols.size() > 1;
  bool show_order_row = !show_overview;
  int selected_row = 0;

  bool is_buy = true;
  bool is_market = false;
  std::string side_label = "Buy";
  std::string type_label = "Limit";

  const auto open_symbol = [&](const std::string& symbol) {
    std::lock_guard lock(store.mu);
    store.symbol = symbol;
    store.latest.reset();
    store.mid_history.clear();
  };

  // Do not use ftxui::Toggle or Menu here: they consume Tab, so keyboard focus can never leave them (FTXUI v5.0.0).
  auto side_btn = Button(&side_label, [&] {
    is_buy = !is_buy;
    side_label = is_buy ? "Buy" : "Sell";
  });
  auto type_btn = Button(&type_label, [&] {
    is_market = !is_market;
    type_label = is_market ? "Market" : "Limit";
  });

  auto submit = Button("Submit", [&] {
    std::int64_t qty = 0;
    if (!parse_i64(qty_str, qty) || qty <= 0) {
      feedback = "Invalid quantity";
      return;
    }
    matching::SubmitRequest req{.symbol = current_symbol(),
                                .side = is_buy ? domain::Side::Buy : domain::Side::Sell,
                                .quantity = qty};
    if (is_market) {
      req.type = domain::OrderType::Market;
      req.limit_price = std::nullopt;
    } else {
      std::int64_t px = 0;
      if (!parse_i64(px_str, px) || px <= 0) {
        feedback = "Invalid price (cents)";
        return;
      }
      req.type = domain::OrderType::Limit;
      req.limit_price = px;
    }
    const auto [code, body] = service.place_order(req, std::nullopt);
    if (code == 200) {
      const std::size_t filled = body.contains("trades") && body["trades"].is_array() ? body["trades"].size() : 0;
      feedback = "Order accepted id=" + std::to_string(body["order_id"].get<std::uint64_t>()) + " filled " +
                 std::to_string(filled) + " trade(s)";
    } else {
      feedback = format_api_error(body);
    }
  });

  auto cancel_btn = Button("Cancel", [&] {
    std::int64_t id = 0;
    if (!parse_i64(cancel_id_str, id) || id <= 0) {
      feedback = "Invalid order id";
      return;
    }
    const auto [code, body] = service.cancel_order(static_cast<domain::OrderId>(id));
    if (code == 200) {
      feedback = "Cancelled id=" + std::to_string(id);
    } else {
      feedback = format_api_error(body);
    }
  });

  const auto qty_in = Input(&qty_str, "qty");
  const auto px_in = Input(&px_str, "cents");
  const auto cancel_in = Input(&cancel_id_str, "order id");

  auto order_row = Container::Horizontal({
      side_btn,
      type_btn,
      qty_in,
      px_in,
      submit,
      cancel_in,
      cancel_btn,
  });

  auto order_row_ui = Renderer(order_row, [&] {
    return hbox({
        text(" Side ") | vcenter,
        side_btn->Render() | color(is_buy ? Color::GreenLight : Color::RedLight),
        text(" Type ") | vcenter,
        type_btn->Render(),
        text(" Qty ") | vcenter,
        qty_in->Render() | size(WIDTH, EQUAL, 8) | border,
        text(" Price ") | vcenter,
        px_in->Render() | size(WIDTH, EQUAL, 10) | border,
        submit->Render(),
        filler(),
        text(" Order id ") | vcenter,
        cancel_in->Render() | size(WIDTH, EQUAL, 10) | border,
        cancel_btn->Render(),
    });
  });

  auto dashboard = Renderer([&] {
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
      return vbox({text("Connecting to simulator…") | hcenter | flex, filler()});
    }

    DashboardRenderModel model = prepare_dashboard_render(*snap, mids);
    if (show_overview) {
      return vbox({
          render_market_overview(model, selected_row) | flex,
          hbox({text(" Up/Down select | Enter open | q quit ") | dim}) | border,
      });
    }
    const Element grid = render_dashboard_grid(model, tw, th);
    const Element foot = hbox({
                             text(" " + feedback + " ") | dim,
                         }) |
                         border;
    return vbox({grid | flex, foot});
  });

  auto root = Container::Vertical({dashboard, Maybe(order_row_ui, &show_order_row)});

  auto wrapped = CatchEvent(root, [&](const Event& e) {
    if (e == Event::Character('q')) {
      worker_stop.store(true, std::memory_order_relaxed);
      screen.Exit();
      return true;
    }
    if (show_overview) {
      const int last_row =
          opts.symbols.empty() ? 0 : static_cast<int>(opts.symbols.size()) - 1;
      if (e == Event::ArrowDown || e == Event::Character('j')) {
        if (selected_row < last_row) {
          ++selected_row;
        }
        return true;
      }
      if (e == Event::ArrowUp || e == Event::Character('k')) {
        if (selected_row > 0) {
          --selected_row;
        }
        return true;
      }
      if (e == Event::Return) {
        if (!opts.symbols.empty()) {
          open_symbol(opts.symbols[static_cast<std::size_t>(selected_row)]);
          show_overview = false;
          show_order_row = true;
        }
        return true;
      }
      if (!e.is_mouse()) {
        return true;
      }
      return false;
    }
    if (e == Event::Character('o') && opts.symbols.size() > 1) {
      show_overview = true;
      show_order_row = false;
      return true;
    }
    if ((e == Event::Character('[') || e == Event::Character(']')) && !opts.symbols.empty()) {
      const int delta = e == Event::Character(']') ? 1 : -1;
      std::size_t idx = 0;
      {
        std::lock_guard lock(store.mu);
        for (std::size_t i = 0; i < opts.symbols.size(); ++i) {
          if (opts.symbols[i] == store.symbol) {
            idx = i;
            break;
          }
        }
      }
      const std::size_t n = opts.symbols.size();
      idx = (idx + static_cast<std::size_t>(delta + static_cast<int>(n))) % n;
      open_symbol(opts.symbols[idx]);
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
