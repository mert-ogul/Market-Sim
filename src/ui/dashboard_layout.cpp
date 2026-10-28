#include "marketsim/ui/dashboard_ftxui.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <string>

namespace marketsim::ui {

namespace {

using namespace ftxui;

[[nodiscard]] Color direction_color(const int direction) {
  if (direction > 0) {
    return Color::GreenLight;
  }
  if (direction < 0) {
    return Color::RedLight;
  }
  return Color::Default;
}

[[nodiscard]] Element depth_table(const char* title, const std::vector<std::array<std::string, 3>>& rows,
                                  const bool is_bid_side, const bool empty_hint) {
  Elements lines;
  lines.push_back(hbox({
                    text(" Price") | flex,
                    text("Qty") | flex,
                    text("#") | flex,
                }) |
                bold);
  if (rows.empty()) {
    lines.push_back(text(empty_hint ? "No bids" : "No asks") | dim);
  } else {
    for (const auto& r : rows) {
      const Color px_color = is_bid_side ? Color::GreenLight : Color::RedLight;
      lines.push_back(hbox({
          text(r[0]) | flex | color(px_color),
          text(" " + r[1]) | flex,
          text(r[2]) | flex,
      }));
    }
  }
  return window(text(title) | bold, vbox(std::move(lines)));
}

[[nodiscard]] Element portfolio_table(const std::vector<std::array<std::string, 5>>& rows) {
  Elements lines;
  lines.push_back(hbox({
                    text("Sym") | flex,
                    text("Qty") | flex,
                    text("Avg") | flex,
                    text("U-PnL") | flex,
                    text("Mkt") | flex,
                }) |
                bold);
  if (rows.empty()) {
    lines.push_back(text("No open positions") | dim);
  } else {
    for (const auto& r : rows) {
      lines.push_back(hbox({
          text(r[0]) | flex,
          text(r[1]) | flex,
          text(r[2]) | flex,
          text(r[3]) | flex,
          text(r[4]) | flex,
      }));
    }
  }
  return window(text("Portfolio") | bold, vbox(std::move(lines)));
}

[[nodiscard]] Element order_rows_table(const char* title, const char* last_column, const char* empty_hint,
                                       const std::vector<std::array<std::string, 5>>& rows,
                                       const int max_rows) {
  Elements lines;
  lines.push_back(hbox({
                    text("Id") | flex,
                    text("Sym") | flex,
                    text("Side") | flex,
                    text("Px") | flex,
                    text(last_column) | flex,
                }) |
                bold);
  if (rows.empty()) {
    lines.push_back(text(empty_hint) | dim);
  } else {
    int shown = 0;
    for (const auto& r : rows) {
      if (shown >= max_rows) {
        break;
      }
      lines.push_back(hbox({
          text(r[0]) | flex,
          text(r[1]) | flex,
          text(r[2]) | flex,
          text(r[3]) | flex,
          text(r[4]) | flex,
      }));
      ++shown;
    }
  }
  return window(text(title) | bold, vbox(std::move(lines)));
}

}

ftxui::Element render_dashboard_grid(const DashboardRenderModel& model, const int term_width,
                                     const int term_height) {
  using namespace ftxui;

  const bool cramped = term_width < 80 || term_height < 24;

  Element header_time = text(model.sim_time_str) | dim;
  if (!model.index_short.empty()) {
    header_time = text(model.index_short) | color(direction_color(model.index_direction));
  }

  Element header = hbox({
      text(" roundlot ") | bold | color(Color::CyanLight),
      text(" | ") | dim,
      text(model.symbol_line) | bold,
      text(" | ") | dim,
      text(model.clock_hhmmss) | color(Color::GrayLight),
      text(" | ") | dim,
      std::move(header_time),
      filler(),
      text("Eq ") | dim,
      text(model.equity_str) | bold,
      text("  P&L r:") | dim,
      text(model.realized_str) | color(Color::GreenLight),
      text(" u:") | dim,
      text(model.unrealized_str) | color(Color::YellowLight),
  });

  if (cramped) {
    header = vbox({
        header,
        paragraph("Tip: use at least 80×24 for the intended layout (now " + std::to_string(term_width) + "×" +
                  std::to_string(term_height) + ")") |
            color(Color::Yellow),
    });
  }

  Element chart = window(
      text("Mid / sparkline") | bold,
      vbox({
          hbox({text("Last: ") | dim, text(model.last_str) | bold, text("  Mid: ") | dim,
                text(model.mid_str), text("  Spread: ") | dim, text(model.spread_str)}),
          separator(),
          paragraph(model.sparkline_line) | color(Color::Cyan),
      }));

  const Element book_row =
      hbox({
          depth_table("Bids", model.bid_table, true, model.orderbook_empty) | flex,
          depth_table("Asks", model.ask_table, false, model.orderbook_empty) | flex,
      }) |
      flex;

  const Element mid_row = hbox({
      book_row | flex,
      chart | flex,
      portfolio_table(model.position_table) | flex,
  });

  const int trade_max = std::max(3, term_height / 3);
  const Element bottom = hbox({
                             order_rows_table("Recent trades", "Qty", "No trades yet", model.trade_table,
                                              trade_max) |
                                 flex,
                             order_rows_table("Open orders", "Rem", "No open orders", model.open_order_table,
                                              trade_max) |
                                 flex,
                         }) |
                         flex;

  return vbox({
             header | border,
             mid_row | flex | border,
             bottom | flex | border,
         }) |
         flex;
}

ftxui::Element render_market_overview(const DashboardRenderModel& model, const int selected_row) {
  using namespace ftxui;

  Element header = hbox({
                       text(" roundlot ") | bold | color(Color::CyanLight),
                       text(" | ") | dim,
                       text(model.index_line) | bold | color(direction_color(model.index_direction)),
                       filler(),
                       text("Eq ") | dim,
                       text(model.equity_str) | bold,
                       text("  P&L r:") | dim,
                       text(model.realized_str) | color(Color::GreenLight),
                       text(" u:") | dim,
                       text(model.unrealized_str) | color(Color::YellowLight),
                   }) |
                   border;

  Elements rows;
  rows.push_back(hbox({
                      text("Symbol") | size(WIDTH, EQUAL, 8) | bold,
                      text("Company") | flex | bold,
                      text("Last") | size(WIDTH, EQUAL, 12) | align_right | bold,
                      text("Change") | size(WIDTH, EQUAL, 10) | align_right | bold,
                      text("Position") | size(WIDTH, EQUAL, 10) | align_right | bold,
                  }) |
                  bold);
  rows.push_back(separator());

  for (std::size_t i = 0; i < model.watch_table.size(); ++i) {
    const auto& r = model.watch_table[i];
    const int direction =
        i < model.watch_direction.size() ? model.watch_direction[i] : 0;
    Element row = hbox({
        text(r[0]) | size(WIDTH, EQUAL, 8) | bold,
        text(r[1]) | flex,
        text(r[2]) | size(WIDTH, EQUAL, 12) | align_right,
        text(r[3]) | size(WIDTH, EQUAL, 10) | align_right | color(direction_color(direction)),
        text(r[4]) | size(WIDTH, EQUAL, 10) | align_right,
    });
    if (static_cast<int>(i) == selected_row) {
      row = std::move(row) | inverted | focus;
    }
    rows.push_back(std::move(row));
  }

  Element table =
      vbox(std::move(rows)) | vscroll_indicator | frame | flex | border;

  return vbox({
             std::move(header),
             std::move(table),
         }) |
         flex;
}

}
