#pragma once

#include <memory>

#include "karma/app/ui_context.h"
#include "karma/scene/node.h"

namespace karma::ecs {
class World;
}

namespace karma::scene {
class Scene;
}

namespace karma::systems {
class SystemGraph;
}

namespace karma::debug {

class DebugOverlayLayer final : public app::UiLayer {
 public:
  DebugOverlayLayer(ecs::World* world,
                    scene::Scene* scene,
                    systems::SystemGraph* systems);

  void onEvent(const platform::Event& event) override;
  void onFrame(app::UIContext& ctx) override;
  void onShutdown() override;

 private:
  ecs::World* world_ = nullptr;
  scene::Scene* scene_ = nullptr;
  systems::SystemGraph* systems_ = nullptr;
  app::UITextureHandle font_texture_ = 0;
  app::UIContext* pending_ctx_ = nullptr;
  scene::NodeId selected_node_ = scene::Node::kInvalidId;
};

}  // namespace karma::debug
