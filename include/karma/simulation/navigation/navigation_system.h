#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "karma/world/ecs/entity.h"
#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_types.h"
#include "karma/world/systems/system.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::navigation {

/// \ingroup karma_navigation
/// Runtime counters and timings for `NavigationSystem`.
struct NavigationSystemStats {
  double last_update_ms = 0.0;
  double last_rebuild_ms = 0.0;
  double last_submit_ms = 0.0;
  double last_move_ms = 0.0;
  double last_apply_ms = 0.0;
  double last_worker_queue_wait_ms = 0.0;
  double last_worker_solve_ms = 0.0;
  uint64_t submitted_requests = 0;
  uint64_t completed_requests = 0;
  uint64_t failed_requests = 0;
  uint64_t stale_results = 0;
  uint64_t pending_requests = 0;
  uint64_t last_request_id = 0;
  uint32_t last_path_point_count = 0;
  NavStatus last_path_status = NavStatus::QueryFailed;
  bool last_worker_cache_rebuilt = false;
};

/// \ingroup karma_navigation
/// Engine-owned async navmesh rebuild/path request system.
///
/// The system consumes `NavMeshComponent` and `NavMeshAgentComponent`, queues
/// worker-thread path solves, and writes path/status data back to agents.
class NavigationSystem : public systems::ISystem {
 public:
  NavigationSystem();
  ~NavigationSystem() override;

  NavigationSystem(const NavigationSystem&) = delete;
  NavigationSystem& operator=(const NavigationSystem&) = delete;
  NavigationSystem(NavigationSystem&&) = delete;
  NavigationSystem& operator=(NavigationSystem&&) = delete;

  std::string_view name() const override { return "NavigationSystem"; }
  void update(ecs::World& world, float dt) override;

  /// Draws current navmesh/path debug information.
  void debugDraw(ecs::World& world,
                 renderer::GraphicsDevice& graphics,
                 bool depth_test = false) const;
  /// Latest navigation diagnostics.
  const NavigationSystemStats& stats() const { return stats_; }

  /// Requests a path for an agent entity.
  static bool requestMoveTo(ecs::World& world,
                            ecs::Entity agent_entity,
                            const math::Vec3& destination);
  /// Clears an agent path/request state.
  static void clearPath(ecs::World& world, ecs::Entity agent_entity);
  /// Requests a crowd-controlled move target for an agent entity.
  static bool requestCrowdMoveTo(ecs::World& world,
                                 ecs::Entity agent_entity,
                                 const math::Vec3& destination);
  /// Requests direct crowd velocity steering for an agent entity.
  static bool requestCrowdVelocity(ecs::World& world,
                                   ecs::Entity agent_entity,
                                   const math::Vec3& velocity);
  /// Clears a crowd-controlled agent target/request state.
  static void clearCrowdTarget(ecs::World& world, ecs::Entity agent_entity);

 private:
  struct WorkerState;

  void submitPathRequests(ecs::World& world);
  void applyCompletedPaths(ecs::World& world);

  std::unique_ptr<WorkerState> worker_;
  uint64_t next_request_id_ = 1;
  NavigationSystemStats stats_{};
};

}  // namespace karma::navigation
