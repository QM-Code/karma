#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "karma/ecs/entity.h"
#include "karma/math/types.h"
#include "karma/navigation/nav_mesh.h"
#include "karma/systems/system.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::navigation {

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

  void debugDraw(ecs::World& world,
                 renderer::GraphicsDevice& graphics,
                 bool depth_test = false) const;
  const NavigationSystemStats& stats() const { return stats_; }

  static bool requestMoveTo(ecs::World& world,
                            ecs::Entity agent_entity,
                            const math::Vec3& destination);
  static void clearPath(ecs::World& world, ecs::Entity agent_entity);

 private:
  struct WorkerState;

  void submitPathRequests(ecs::World& world);
  void applyCompletedPaths(ecs::World& world);

  std::unique_ptr<WorkerState> worker_;
  uint64_t next_request_id_ = 1;
  NavigationSystemStats stats_{};
};

}  // namespace karma::navigation
