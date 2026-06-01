#include "karma/simulation/navigation/navigation_diagnostics.h"

#include <cstdlib>
#include <cstring>

#include <spdlog/spdlog.h>

namespace karma::navigation {
namespace {

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

}  // namespace

void NavigationDiagnostics::initializeFromEnvironment() {
  enabled_ = envFlagEnabled(std::getenv("KARMA_NAVMESH_DIAG"));
  if (enabled_) {
    spdlog::info("KARMA_NAVMESH_DIAG enabled; logging nav request diagnostics");
  }
}

void NavigationDiagnostics::logIfChanged(const NavigationSystemStats& stats,
                                         const NavigationDiagnosticsFrame& frame) {
  if (!enabled_) {
    return;
  }

  if (stats.submitted_requests == logged_submitted_requests_ &&
      stats.completed_requests == logged_completed_requests_ &&
      stats.failed_requests == logged_failed_requests_ &&
      stats.stale_results == logged_stale_results_) {
    return;
  }

  logged_submitted_requests_ = stats.submitted_requests;
  logged_completed_requests_ = stats.completed_requests;
  logged_failed_requests_ = stats.failed_requests;
  logged_stale_results_ = stats.stale_results;

  spdlog::info(
      "Nav diag: main update={:.3f}ms rebuild={:.3f} submit={:.3f} move={:.3f} apply={:.3f}; "
      "worker queue={:.3f}ms solve={:.3f}ms cache_rebuilt={}; requests submitted={} completed={} "
      "failed={} stale={} pending={} last={} status={} points={}; "
      "example frame_dt={:.3f}ms on_update={:.3f} click={:.3f} camera={:.3f} debug_draw={:.3f}",
      stats.last_update_ms,
      stats.last_rebuild_ms,
      stats.last_submit_ms,
      stats.last_move_ms,
      stats.last_apply_ms,
      stats.last_worker_queue_wait_ms,
      stats.last_worker_solve_ms,
      stats.last_worker_cache_rebuilt,
      stats.submitted_requests,
      stats.completed_requests,
      stats.failed_requests,
      stats.stale_results,
      stats.pending_requests,
      stats.last_request_id,
      navigation::navStatusName(stats.last_path_status),
      stats.last_path_point_count,
      frame.dt * 1000.0f,
      frame.on_update_ms,
      frame.click_ms,
      frame.camera_ms,
      frame.debug_draw_ms);
}

}  // namespace karma::navigation
