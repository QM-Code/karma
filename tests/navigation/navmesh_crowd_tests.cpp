#include "navmesh_test_utils.h"

namespace karma::tests::navigation {

void testCrowdMovesAgentToTarget() {
  karma::navigation::NavMeshBuildConfig nav_config;
  nav_config.agent_radius = 0.2f;
  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry(), nav_config));

  karma::navigation::NavCrowd crowd;
  karma::navigation::NavCrowdConfig crowd_config;
  crowd_config.max_agents = 8;
  crowd_config.max_agent_radius = 0.4f;
  assert(crowd.init(nav_mesh, crowd_config));

  karma::navigation::NavCrowdAgentParams params;
  params.radius = 0.2f;
  params.height = 1.0f;
  params.max_speed = 2.5f;
  params.max_acceleration = 12.0f;
  const int agent_id = crowd.addAgent({-4.0f, 0.1f, 0.0f}, params);
  assert(agent_id >= 0);
  assert(crowd.activeAgentCount() == 1);
  assert(crowd.requestMoveTarget(agent_id, {4.0f, 0.1f, 0.0f}));

  karma::navigation::NavCrowdAgentInfo info;
  for (int i = 0; i < 80; ++i) {
    crowd.update(0.1f);
    assert(crowd.agentInfo(agent_id, info));
    if (std::abs(info.position.x - 4.0f) < 0.5f) {
      break;
    }
  }
  assert(info.active);
  assert(info.state == karma::navigation::NavCrowdAgentState::Walking);
  assert(std::abs(info.position.x - 4.0f) < 0.75f);

  assert(crowd.requestMoveVelocity(agent_id, {-1.0f, 0.0f, 0.0f}));
  crowd.update(0.2f);
  assert(crowd.agentInfo(agent_id, info));
  assert(info.target_state == karma::navigation::NavCrowdTargetState::Velocity);

  const karma::navigation::NavCrowdDebugSnapshot debug = crowd.debugSnapshot({
      .enabled = true,
      .all_agents = true,
  });
  assert(!debug.empty());
  assert(debug.agents.front().agent_id == agent_id);
  assert(!debug.agents.front().corridor_polys.empty());
}

}  // namespace karma::tests::navigation
