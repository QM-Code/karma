#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_mesh.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::navigation {

/// \ingroup karma_navigation
/// Crowd steering feature bits mirroring DetourCrowd update flags.
enum NavCrowdUpdateFlag : uint8_t {
  NavCrowdUpdateFlagNone = 0,
  NavCrowdUpdateFlagAnticipateTurns = 1u << 0u,
  NavCrowdUpdateFlagObstacleAvoidance = 1u << 1u,
  NavCrowdUpdateFlagSeparation = 1u << 2u,
  NavCrowdUpdateFlagOptimizeVisibility = 1u << 3u,
  NavCrowdUpdateFlagOptimizeTopology = 1u << 4u,
};

/// \ingroup karma_navigation
/// Agent navmesh traversal state reported by DetourCrowd.
enum class NavCrowdAgentState {
  Invalid,
  Walking,
  OffMesh,
};

/// \ingroup karma_navigation
/// Agent move-request state reported by DetourCrowd.
enum class NavCrowdTargetState {
  None,
  Failed,
  Valid,
  Requesting,
  WaitingForQueue,
  WaitingForPath,
  Velocity,
};

/// \ingroup karma_navigation
/// Obstacle-avoidance sampler configuration for one DetourCrowd quality slot.
struct NavCrowdObstacleAvoidanceParams {
  float velocity_bias = 0.5f;
  float weight_desired_velocity = 2.0f;
  float weight_current_velocity = 0.75f;
  float weight_side = 0.75f;
  float weight_time_of_impact = 2.5f;
  float horizontal_time = 2.5f;
  uint8_t grid_size = 33;
  uint8_t adaptive_divisions = 7;
  uint8_t adaptive_rings = 2;
  uint8_t adaptive_depth = 2;
};

/// \ingroup karma_navigation
/// Shared crowd configuration.
struct NavCrowdConfig {
  int max_agents = 64;
  float max_agent_radius = 0.6f;
  math::Vec3 query_extents{2.0f, 4.0f, 2.0f};
  std::vector<NavQueryFilter> query_filters;
  std::vector<NavCrowdObstacleAvoidanceParams> avoidance_params;
};

/// \ingroup karma_navigation
/// Per-agent crowd steering configuration.
struct NavCrowdAgentParams {
  float radius = 0.6f;
  float height = 2.0f;
  float max_acceleration = 8.0f;
  float max_speed = 3.5f;
  float collision_query_range = 0.0f;
  float path_optimization_range = 0.0f;
  float separation_weight = 2.0f;
  uint8_t update_flags = NavCrowdUpdateFlagAnticipateTurns |
                         NavCrowdUpdateFlagObstacleAvoidance |
                         NavCrowdUpdateFlagOptimizeVisibility |
                         NavCrowdUpdateFlagOptimizeTopology;
  uint8_t obstacle_avoidance_type = 2;
  uint8_t query_filter_type = 0;
};

/// \ingroup karma_navigation
/// Crowd initialization result.
struct NavCrowdBuildResult {
  NavStatus status = NavStatus::BuildFailed;
  std::string message;
  uint32_t agent_capacity = 0;
};

/// \ingroup karma_navigation
/// Runtime crowd agent state.
struct NavCrowdAgentInfo {
  int agent_id = -1;
  bool active = false;
  bool partial = false;
  NavCrowdAgentState state = NavCrowdAgentState::Invalid;
  NavCrowdTargetState target_state = NavCrowdTargetState::None;
  math::Vec3 position{};
  math::Vec3 velocity{};
  math::Vec3 desired_velocity{};
  math::Vec3 adjusted_desired_velocity{};
  math::Vec3 target_position{};
  float desired_speed = 0.0f;
  float radius = 0.0f;
  float height = 0.0f;
  int neighbor_count = 0;
  int corner_count = 0;
};

/// \ingroup karma_navigation
/// DetourCrowd wrapper for local steering and dynamic avoidance over a `NavMesh`.
class NavCrowd {
 public:
  NavCrowd();
  ~NavCrowd();

  NavCrowd(const NavCrowd&) = delete;
  NavCrowd& operator=(const NavCrowd&) = delete;
  NavCrowd(NavCrowd&&) noexcept;
  NavCrowd& operator=(NavCrowd&&) noexcept;

  /// Initializes crowd steering against a valid navmesh.
  bool init(NavMesh& nav_mesh,
            const NavCrowdConfig& config = {},
            NavCrowdBuildResult* result = nullptr);
  /// Releases DetourCrowd state.
  void reset();
  /// Returns true after successful initialization.
  bool isValid() const;
  /// Maximum configured agent count.
  int agentCapacity() const;
  /// Current active agent count.
  int activeAgentCount() const;

  /// Adds a crowd agent at a navmesh position, returning its Detour slot id or -1.
  int addAgent(const math::Vec3& position, const NavCrowdAgentParams& params = {});
  /// Updates a crowd agent's steering parameters.
  bool updateAgentParams(int agent_id, const NavCrowdAgentParams& params);
  /// Removes a crowd agent slot.
  void removeAgent(int agent_id);
  /// Requests a navmesh target for one crowd agent.
  bool requestMoveTarget(int agent_id,
                         const math::Vec3& target,
                         const math::Vec3& search_extents = {0.0f, 0.0f, 0.0f});
  /// Requests direct velocity steering for one crowd agent.
  bool requestMoveVelocity(int agent_id, const math::Vec3& velocity);
  /// Clears a crowd agent's target/velocity request.
  bool resetMoveTarget(int agent_id);
  /// Advances crowd steering.
  void update(float dt);
  /// Returns current state for one agent.
  bool agentInfo(int agent_id, NavCrowdAgentInfo& out_info) const;
  /// Returns all active agents.
  std::vector<NavCrowdAgentInfo> agents() const;
  /// Updates one query filter slot.
  bool setQueryFilter(uint8_t filter_index, const NavQueryFilter& filter);
  /// Updates one obstacle avoidance quality slot.
  bool setObstacleAvoidanceParams(uint8_t slot, const NavCrowdObstacleAvoidanceParams& params);
  /// Metadata from the last initialization attempt.
  const NavCrowdBuildResult& lastBuildResult() const;
  /// Draws active crowd agents, velocities, and move targets through the graphics device.
  void debugDraw(renderer::GraphicsDevice& graphics,
                 const math::Color& agent_color = {0.2f, 0.65f, 1.0f, 1.0f},
                 const math::Color& velocity_color = {0.95f, 0.95f, 0.15f, 1.0f},
                 bool depth_test = false) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::navigation
