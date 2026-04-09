#pragma once

#include "marketsim/ui/dashboard_prepare.hpp"

#include <ftxui/dom/elements.hpp>

namespace marketsim::ui {

/// Renders the main grid from a pre-built render model (no engine access).
[[nodiscard]] ftxui::Element render_dashboard_grid(const DashboardRenderModel& model, int term_width,
                                                   int term_height);

}
