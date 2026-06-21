#include "karma/navigation.h"
#include "karma/navigation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourObstacleAvoidance.h>
#include <DetourStatus.h>

#include "karma/math.h"
#include "karma/rendering.h"
#include "detail/detour_utils.h"
#include "detail/nav_mesh_access.h"

namespace karma::navigation {
namespace {

constexpr float kPi = 3.14159265358979323846f;

using detail::applyDetourFilter;
using detail::failed;
using detail::mapStraightPathFlags;
using detail::ptr;
using detail::toVec3;

void drawCircle(rendering::GraphicsDevice& graphics,
                const math::Vec3& center,
                float radius,
                const math::Color& color,
                bool depth_test) {
  constexpr int kSegments = 28;
  math::Vec3 prev{center.x + radius, center.y, center.z};
  for (int i = 1; i <= kSegments; ++i) {
    const float angle = (static_cast<float>(i) / static_cast<float>(kSegments)) * kPi * 2.0f;
    const math::Vec3 next{
        center.x + std::cos(angle) * radius,
        center.y,
        center.z + std::sin(angle) * radius,
    };
    graphics.drawLine(prev, next, color, depth_test, 1.0f);
    prev = next;
  }
}

void drawCross(rendering::GraphicsDevice& graphics,
               const math::Vec3& center,
               float size,
               const math::Color& color,
               bool depth_test) {
  graphics.drawLine({center.x - size, center.y, center.z},
                    {center.x + size, center.y, center.z},
                    color,
                    depth_test,
                    1.0f);
  graphics.drawLine({center.x, center.y, center.z - size},
                    {center.x, center.y, center.z + size},
                    color,
                    depth_test,
                    1.0f);
}

void setResult(NavCrowdBuildResult* result,
               NavStatus status,
               std::string message,
               uint32_t agent_capacity = 0) {
  if (result == nullptr) {
    return;
  }
  result->status = status;
  result->message = std::move(message);
  result->agent_capacity = agent_capacity;
}

dtCrowdAgentParams toDetourParams(const NavCrowdAgentParams& params) {
  dtCrowdAgentParams out{};
  out.radius = params.radius;
  out.height = params.height;
  out.maxAcceleration = params.max_acceleration;
  out.maxSpeed = params.max_speed;
  out.collisionQueryRange = params.collision_query_range > 0.0f
      ? params.collision_query_range
      : params.radius * 12.0f;
  out.pathOptimizationRange = params.path_optimization_range > 0.0f
      ? params.path_optimization_range
      : params.radius * 30.0f;
  out.separationWeight = params.separation_weight;
  out.updateFlags = 0;
  if ((params.update_flags & NavCrowdUpdateFlagAnticipateTurns) != 0) {
    out.updateFlags |= DT_CROWD_ANTICIPATE_TURNS;
  }
  if ((params.update_flags & NavCrowdUpdateFlagObstacleAvoidance) != 0) {
    out.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
  }
  if ((params.update_flags & NavCrowdUpdateFlagSeparation) != 0) {
    out.updateFlags |= DT_CROWD_SEPARATION;
  }
  if ((params.update_flags & NavCrowdUpdateFlagOptimizeVisibility) != 0) {
    out.updateFlags |= DT_CROWD_OPTIMIZE_VIS;
  }
  if ((params.update_flags & NavCrowdUpdateFlagOptimizeTopology) != 0) {
    out.updateFlags |= DT_CROWD_OPTIMIZE_TOPO;
  }
  out.obstacleAvoidanceType =
      static_cast<unsigned char>(std::min<int>(params.obstacle_avoidance_type,
                                               DT_CROWD_MAX_OBSTAVOIDANCE_PARAMS - 1));
  out.queryFilterType =
      static_cast<unsigned char>(std::min<int>(params.query_filter_type,
                                               DT_CROWD_MAX_QUERY_FILTER_TYPE - 1));
  out.userData = nullptr;
  return out;
}

dtObstacleAvoidanceParams toDetourAvoidance(const NavCrowdObstacleAvoidanceParams& params) {
  dtObstacleAvoidanceParams out{};
  out.velBias = params.velocity_bias;
  out.weightDesVel = params.weight_desired_velocity;
  out.weightCurVel = params.weight_current_velocity;
  out.weightSide = params.weight_side;
  out.weightToi = params.weight_time_of_impact;
  out.horizTime = params.horizontal_time;
  out.gridSize = params.grid_size;
  out.adaptiveDivs = params.adaptive_divisions;
  out.adaptiveRings = params.adaptive_rings;
  out.adaptiveDepth = params.adaptive_depth;
  return out;
}

std::vector<NavCrowdObstacleAvoidanceParams> defaultAvoidanceParams() {
  NavCrowdObstacleAvoidanceParams low;
  low.adaptive_divisions = 5;
  low.adaptive_rings = 2;
  low.adaptive_depth = 1;

  NavCrowdObstacleAvoidanceParams medium = low;
  medium.adaptive_depth = 2;

  NavCrowdObstacleAvoidanceParams good;
  good.adaptive_divisions = 7;
  good.adaptive_rings = 2;
  good.adaptive_depth = 3;

  NavCrowdObstacleAvoidanceParams high = good;
  high.adaptive_rings = 3;
  return {low, medium, good, high};
}

NavCrowdAgentState mapAgentState(unsigned char state) {
  switch (state) {
    case DT_CROWDAGENT_STATE_WALKING: return NavCrowdAgentState::Walking;
    case DT_CROWDAGENT_STATE_OFFMESH: return NavCrowdAgentState::OffMesh;
    case DT_CROWDAGENT_STATE_INVALID:
    default:
      return NavCrowdAgentState::Invalid;
  }
}

NavCrowdTargetState mapTargetState(unsigned char state) {
  switch (state) {
    case DT_CROWDAGENT_TARGET_FAILED: return NavCrowdTargetState::Failed;
    case DT_CROWDAGENT_TARGET_VALID: return NavCrowdTargetState::Valid;
    case DT_CROWDAGENT_TARGET_REQUESTING: return NavCrowdTargetState::Requesting;
    case DT_CROWDAGENT_TARGET_WAITING_FOR_QUEUE: return NavCrowdTargetState::WaitingForQueue;
    case DT_CROWDAGENT_TARGET_WAITING_FOR_PATH: return NavCrowdTargetState::WaitingForPath;
    case DT_CROWDAGENT_TARGET_VELOCITY: return NavCrowdTargetState::Velocity;
    case DT_CROWDAGENT_TARGET_NONE:
    default:
      return NavCrowdTargetState::None;
  }
}

bool validAgentId(dtCrowd& crowd, int agent_id) {
  if (agent_id < 0 || agent_id >= crowd.getAgentCount()) {
    return false;
  }
  const dtCrowdAgent* agent = crowd.getAgent(agent_id);
  return agent != nullptr && agent->active;
}

}  // namespace

struct NavCrowd::Impl {
  ~Impl() {
    reset();
  }

  void reset() {
    if (crowd != nullptr) {
      dtFreeCrowd(crowd);
      crowd = nullptr;
    }
    config = {};
    last_result = {};
  }

  dtCrowd* crowd = nullptr;
  NavCrowdConfig config{};
  NavCrowdBuildResult last_result{};
};

NavCrowd::NavCrowd()
    : impl_(std::make_unique<Impl>()) {}

NavCrowd::~NavCrowd() = default;

NavCrowd::NavCrowd(NavCrowd&&) noexcept = default;

NavCrowd& NavCrowd::operator=(NavCrowd&&) noexcept = default;

bool NavCrowd::init(NavMesh& nav_mesh,
                    const NavCrowdConfig& config,
                    NavCrowdBuildResult* result) {
  if (impl_ == nullptr) {
    impl_ = std::make_unique<Impl>();
  }
  impl_->reset();

  dtNavMesh* detour_nav_mesh = detail::NavMeshAccess::detour(nav_mesh);
  if (detour_nav_mesh == nullptr || !nav_mesh.isValid()) {
    setResult(result, NavStatus::NoNavMesh, "Cannot initialize crowd without a valid navmesh.");
    impl_->last_result = result != nullptr
        ? *result
        : NavCrowdBuildResult{NavStatus::NoNavMesh,
                              "Cannot initialize crowd without a valid navmesh.",
                              0};
    return false;
  }
  if (config.max_agents <= 0 || config.max_agent_radius <= 0.0f) {
    setResult(result, NavStatus::InvalidConfig, "Navigation crowd config is invalid.");
    impl_->last_result = result != nullptr
        ? *result
        : NavCrowdBuildResult{NavStatus::InvalidConfig, "Navigation crowd config is invalid.", 0};
    return false;
  }

  impl_->crowd = dtAllocCrowd();
  if (impl_->crowd == nullptr ||
      !impl_->crowd->init(config.max_agents, config.max_agent_radius, detour_nav_mesh)) {
    setResult(result, NavStatus::BuildFailed, "Failed to initialize Detour crowd.");
    impl_->last_result = result != nullptr
        ? *result
        : NavCrowdBuildResult{NavStatus::BuildFailed, "Failed to initialize Detour crowd.", 0};
    return false;
  }

  impl_->config = config;
  const std::vector<NavQueryFilter> filters = config.query_filters.empty()
      ? std::vector<NavQueryFilter>{NavQueryFilter{}}
      : config.query_filters;
  for (size_t i = 0; i < filters.size() && i < DT_CROWD_MAX_QUERY_FILTER_TYPE; ++i) {
    dtQueryFilter* filter = impl_->crowd->getEditableFilter(static_cast<int>(i));
    if (filter != nullptr) {
      applyDetourFilter(*filter, filters[i]);
    }
  }

  const std::vector<NavCrowdObstacleAvoidanceParams> avoidance_params =
      config.avoidance_params.empty() ? defaultAvoidanceParams() : config.avoidance_params;
  for (size_t i = 0; i < avoidance_params.size() && i < DT_CROWD_MAX_OBSTAVOIDANCE_PARAMS; ++i) {
    const dtObstacleAvoidanceParams params = toDetourAvoidance(avoidance_params[i]);
    impl_->crowd->setObstacleAvoidanceParams(static_cast<int>(i), &params);
  }

  NavCrowdBuildResult success{};
  success.status = NavStatus::Success;
  success.message = "Navigation crowd initialized.";
  success.agent_capacity = static_cast<uint32_t>(config.max_agents);
  impl_->last_result = success;
  if (result != nullptr) {
    *result = success;
  }
  return true;
}

void NavCrowd::reset() {
  if (impl_ != nullptr) {
    impl_->reset();
  }
}

bool NavCrowd::isValid() const {
  return impl_ != nullptr && impl_->crowd != nullptr;
}

int NavCrowd::agentCapacity() const {
  return isValid() ? impl_->crowd->getAgentCount() : 0;
}

int NavCrowd::activeAgentCount() const {
  if (!isValid()) {
    return 0;
  }
  int count = 0;
  for (int i = 0; i < impl_->crowd->getAgentCount(); ++i) {
    const dtCrowdAgent* agent = impl_->crowd->getAgent(i);
    if (agent != nullptr && agent->active) {
      ++count;
    }
  }
  return count;
}

int NavCrowd::addAgent(const math::Vec3& position, const NavCrowdAgentParams& params) {
  if (!isValid() ||
      params.radius < 0.0f ||
      params.height <= 0.0f ||
      params.max_acceleration < 0.0f ||
      params.max_speed < 0.0f) {
    return -1;
  }
  const dtCrowdAgentParams detour_params = toDetourParams(params);
  return impl_->crowd->addAgent(ptr(position), &detour_params);
}

bool NavCrowd::updateAgentParams(int agent_id, const NavCrowdAgentParams& params) {
  if (!isValid() || !validAgentId(*impl_->crowd, agent_id)) {
    return false;
  }
  const dtCrowdAgentParams detour_params = toDetourParams(params);
  impl_->crowd->updateAgentParameters(agent_id, &detour_params);
  return true;
}

void NavCrowd::removeAgent(int agent_id) {
  if (!isValid() || agent_id < 0 || agent_id >= impl_->crowd->getAgentCount()) {
    return;
  }
  impl_->crowd->removeAgent(agent_id);
}

bool NavCrowd::requestMoveTarget(int agent_id,
                                 const math::Vec3& target,
                                 const math::Vec3& search_extents) {
  if (!isValid() || !validAgentId(*impl_->crowd, agent_id)) {
    return false;
  }
  const dtCrowdAgent* agent = impl_->crowd->getAgent(agent_id);
  const dtQueryFilter* filter = impl_->crowd->getFilter(agent->params.queryFilterType);
  const dtNavMeshQuery* query = impl_->crowd->getNavMeshQuery();
  if (filter == nullptr || query == nullptr) {
    return false;
  }

  const math::Vec3 extents =
      search_extents.x > 0.0f || search_extents.y > 0.0f || search_extents.z > 0.0f
          ? search_extents
          : impl_->config.query_extents;
  dtPolyRef target_ref = 0;
  float nearest[3]{};
  if (failed(query->findNearestPoly(ptr(target), ptr(extents), filter, &target_ref, nearest)) ||
      target_ref == 0) {
    return false;
  }
  return impl_->crowd->requestMoveTarget(agent_id, target_ref, nearest);
}

bool NavCrowd::requestMoveVelocity(int agent_id, const math::Vec3& velocity) {
  if (!isValid() || !validAgentId(*impl_->crowd, agent_id)) {
    return false;
  }
  return impl_->crowd->requestMoveVelocity(agent_id, ptr(velocity));
}

bool NavCrowd::resetMoveTarget(int agent_id) {
  if (!isValid() || !validAgentId(*impl_->crowd, agent_id)) {
    return false;
  }
  return impl_->crowd->resetMoveTarget(agent_id);
}

void NavCrowd::update(float dt) {
  if (!isValid() || dt <= 0.0f) {
    return;
  }
  impl_->crowd->update(dt, nullptr);
}

NavCrowdDebugSnapshot NavCrowd::debugSnapshot(const NavCrowdDebugRequest& request) const {
  NavCrowdDebugSnapshot snapshot;
  if (!isValid()) {
    return snapshot;
  }
  for (int i = 0; i < impl_->crowd->getAgentCount(); ++i) {
    if (!request.all_agents && i != request.selected_agent_id) {
      continue;
    }
    const dtCrowdAgent* agent = impl_->crowd->getAgent(i);
    if (agent == nullptr || !agent->active) {
      continue;
    }

    NavCrowdDebugAgent debug_agent;
    debug_agent.agent_id = i;
    if (request.include_corridor) {
      const dtPolyRef* path = agent->corridor.getPath();
      const int path_count = agent->corridor.getPathCount();
      debug_agent.corridor_polys.reserve(static_cast<size_t>(path_count));
      for (int path_index = 0; path_index < path_count; ++path_index) {
        debug_agent.corridor_polys.push_back(static_cast<uint64_t>(path[path_index]));
      }
    }
    if (request.include_corners) {
      debug_agent.corners.reserve(static_cast<size_t>(agent->ncorners));
      for (int corner_index = 0; corner_index < agent->ncorners; ++corner_index) {
        debug_agent.corners.push_back({
            .position = toVec3(&agent->cornerVerts[corner_index * 3]),
            .flags = mapStraightPathFlags(agent->cornerFlags[corner_index]),
            .poly_ref = static_cast<uint64_t>(agent->cornerPolys[corner_index]),
        });
      }
    }
    if (request.include_collision_segments) {
      const int segment_count = agent->boundary.getSegmentCount();
      debug_agent.boundary_segments.reserve(static_cast<size_t>(segment_count));
      for (int segment_index = 0; segment_index < segment_count; ++segment_index) {
        const float* segment = agent->boundary.getSegment(segment_index);
        debug_agent.boundary_segments.push_back({
            .start = {segment[0], segment[1], segment[2]},
            .end = {segment[3], segment[4], segment[5]},
        });
      }
    }
    if (request.include_neighbours) {
      debug_agent.neighbours.reserve(static_cast<size_t>(agent->nneis));
      for (int neighbour_index = 0; neighbour_index < agent->nneis; ++neighbour_index) {
        debug_agent.neighbours.push_back({
            .agent_id = agent->neis[neighbour_index].idx,
            .distance = agent->neis[neighbour_index].dist,
        });
      }
    }
    snapshot.agents.push_back(std::move(debug_agent));
  }
  return snapshot;
}

bool NavCrowd::agentInfo(int agent_id, NavCrowdAgentInfo& out_info) const {
  if (!isValid() || agent_id < 0 || agent_id >= impl_->crowd->getAgentCount()) {
    return false;
  }
  const dtCrowdAgent* agent = impl_->crowd->getAgent(agent_id);
  if (agent == nullptr || !agent->active) {
    return false;
  }
  out_info.agent_id = agent_id;
  out_info.active = agent->active;
  out_info.partial = agent->partial;
  out_info.state = mapAgentState(agent->state);
  out_info.target_state = mapTargetState(agent->targetState);
  out_info.position = toVec3(agent->npos);
  out_info.velocity = toVec3(agent->vel);
  out_info.desired_velocity = toVec3(agent->dvel);
  out_info.adjusted_desired_velocity = toVec3(agent->nvel);
  out_info.target_position = toVec3(agent->targetPos);
  out_info.desired_speed = agent->desiredSpeed;
  out_info.radius = agent->params.radius;
  out_info.height = agent->params.height;
  out_info.neighbor_count = agent->nneis;
  out_info.corner_count = agent->ncorners;
  return true;
}

std::vector<NavCrowdAgentInfo> NavCrowd::agents() const {
  std::vector<NavCrowdAgentInfo> out;
  if (!isValid()) {
    return out;
  }
  for (int i = 0; i < impl_->crowd->getAgentCount(); ++i) {
    NavCrowdAgentInfo info;
    if (agentInfo(i, info)) {
      out.push_back(info);
    }
  }
  return out;
}

bool NavCrowd::setQueryFilter(uint8_t filter_index, const NavQueryFilter& filter) {
  if (!isValid() || filter_index >= DT_CROWD_MAX_QUERY_FILTER_TYPE) {
    return false;
  }
  dtQueryFilter* detour_filter = impl_->crowd->getEditableFilter(filter_index);
  if (detour_filter == nullptr) {
    return false;
  }
  applyDetourFilter(*detour_filter, filter);
  if (impl_->config.query_filters.size() <= filter_index) {
    impl_->config.query_filters.resize(static_cast<size_t>(filter_index) + 1u);
  }
  impl_->config.query_filters[filter_index] = filter;
  return true;
}

bool NavCrowd::setObstacleAvoidanceParams(uint8_t slot,
                                          const NavCrowdObstacleAvoidanceParams& params) {
  if (!isValid() || slot >= DT_CROWD_MAX_OBSTAVOIDANCE_PARAMS) {
    return false;
  }
  const dtObstacleAvoidanceParams detour_params = toDetourAvoidance(params);
  impl_->crowd->setObstacleAvoidanceParams(slot, &detour_params);
  if (impl_->config.avoidance_params.size() <= slot) {
    impl_->config.avoidance_params.resize(static_cast<size_t>(slot) + 1u);
  }
  impl_->config.avoidance_params[slot] = params;
  return true;
}

const NavCrowdBuildResult& NavCrowd::lastBuildResult() const {
  static const NavCrowdBuildResult empty{};
  return impl_ != nullptr ? impl_->last_result : empty;
}

void NavCrowd::debugDraw(rendering::GraphicsDevice& graphics,
                         const math::Color& agent_color,
                         const math::Color& velocity_color,
                         bool depth_test) const {
  for (const NavCrowdAgentInfo& agent : agents()) {
    const float radius = std::max(agent.radius, 0.1f);
    drawCircle(graphics, agent.position, radius, agent_color, depth_test);
    drawCircle(graphics,
               {agent.position.x, agent.position.y + std::max(agent.height, 0.0f), agent.position.z},
               radius,
               agent_color,
               depth_test);

    const math::Vec3 velocity_end =
        math::add(agent.position, math::scale(agent.velocity, 0.35f));
    graphics.drawLine(agent.position, velocity_end, velocity_color, depth_test, 1.5f);

    if (agent.target_state != NavCrowdTargetState::None &&
        agent.target_state != NavCrowdTargetState::Failed) {
      drawCross(graphics, agent.target_position, radius * 1.4f, velocity_color, depth_test);
      graphics.drawLine(agent.position, agent.target_position, velocity_color, depth_test, 1.0f);
    }
  }
}

}  // namespace karma::navigation
