#pragma once

#include <cstdint>

#include "karma/simulation/navigation/navigation_system.h"

namespace karma::navigation {

/// \ingroup karma_navigation
/// Per-frame timings logged by navigation diagnostics.
struct NavigationDiagnosticsFrame {
  float dt = 0.0f;
  double on_update_ms = 0.0;
  double click_ms = 0.0;
  double camera_ms = 0.0;
  double debug_draw_ms = 0.0;
};

/// \ingroup karma_navigation
/// Environment-gated navigation diagnostics logger.
class NavigationDiagnostics {
 public:
  void initializeFromEnvironment();
  bool enabled() const { return enabled_; }
  void logIfChanged(const NavigationSystemStats& stats,
                    const NavigationDiagnosticsFrame& frame);

 private:
  bool enabled_ = false;
  uint64_t logged_submitted_requests_ = 0;
  uint64_t logged_completed_requests_ = 0;
  uint64_t logged_failed_requests_ = 0;
  uint64_t logged_stale_results_ = 0;
};

}  // namespace karma::navigation
