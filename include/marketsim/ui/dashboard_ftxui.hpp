#pragma once

#include "marketsim/ui/dashboard_prepare.hpp"

#include <ftxui/dom/elements.hpp>

namespace marketsim::ui {

/// Renders the main grid from a pre-built render model (no engine access).
[[nodiscard]] ftxui::Element render_dashboard_grid(const DashboardRenderModel& model, int term_width,
                                                   int term_height);

/// All symbols in one table with the index on top; selected_row is highlighted and kept in view.
[[nodiscard]] ftxui::Element render_market_overview(const DashboardRenderModel& model, int selected_row);

}
