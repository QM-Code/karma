#include "karma/navigation.h"

#include <algorithm>
#include <cmath>

#include "karma/math.h"
#include "karma/rendering.h"

namespace karma::navigation {
namespace {

constexpr float kPi = 3.14159265358979323846f;

void drawCircle(rendering::GraphicsDevice& graphics,
                const math::Vec3& center,
                float radius,
                const math::Color& color,
                bool depth_test) {
  constexpr int kSegments = 28;
  math::Vec3 previous{center.x + radius, center.y, center.z};
  for (int index = 1; index <= kSegments; ++index) {
    const float angle =
        (static_cast<float>(index) / static_cast<float>(kSegments)) *
        kPi * 2.0f;
    const math::Vec3 next{
        center.x + std::cos(angle) * radius,
        center.y,
        center.z + std::sin(angle) * radius,
    };
    graphics.drawLine(previous, next, color, depth_test, 1.0f);
    previous = next;
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

}  // namespace

void NavCrowd::debugDraw(rendering::GraphicsDevice& graphics,
                         const math::Color& agent_color,
                         const math::Color& velocity_color,
                         bool depth_test) const {
  for (const NavCrowdAgentInfo& agent : agents()) {
    const float radius = std::max(agent.radius, 0.1f);
    drawCircle(graphics, agent.position, radius, agent_color, depth_test);
    drawCircle(graphics,
               {agent.position.x,
                agent.position.y + std::max(agent.height, 0.0f),
                agent.position.z},
               radius,
               agent_color,
               depth_test);

    const math::Vec3 velocity_end =
        math::add(agent.position, math::scale(agent.velocity, 0.35f));
    graphics.drawLine(agent.position,
                      velocity_end,
                      velocity_color,
                      depth_test,
                      1.5f);

    if (agent.target_state != NavCrowdTargetState::None &&
        agent.target_state != NavCrowdTargetState::Failed) {
      drawCross(graphics,
                agent.target_position,
                radius * 1.4f,
                velocity_color,
                depth_test);
      graphics.drawLine(agent.position,
                        agent.target_position,
                        velocity_color,
                        depth_test,
                        1.0f);
    }
  }
}

}  // namespace karma::navigation
