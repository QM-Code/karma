#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "karma/runtime/app/ui_context.h"
#include "karma/world/scene/node.h"

struct ImGuiContext;

namespace karma::ecs {
class World;
}

namespace karma::scene {
class Scene;
}

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::systems {
class SystemGraph;
}

namespace karma::debug {

class DebugOverlayLayer final : public app::UiLayer {
 public:
  DebugOverlayLayer(ecs::World* world,
                    scene::Scene* scene,
                    systems::SystemGraph* systems,
                    renderer::GraphicsDevice* graphics,
                    int shadow_map_size,
                    float shadow_bias,
                    int shadow_pcf_radius,
                    int shadow_raster_depth_bias,
                    float shadow_raster_slope_bias,
                    float shadow_receiver_bias_scale,
                    float shadow_normal_bias_scale,
                    float point_shadow_constant_bias,
                    float point_shadow_slope_bias_scale,
                    float point_shadow_normal_bias_scale,
                    float point_shadow_receiver_bias_scale,
                    float local_light_distance_damping,
                    float local_light_range_falloff_exponent,
                    bool ao_affects_local_lights,
                    float local_light_directional_shadow_lift_strength,
                    float lighting_exposure,
                    int forward_plus_max_local_lights);

  void onEvent(const platform::Event& event) override;
  void onFrame(app::UIContext& ctx) override;
  void onShutdown() override;

 private:
  ecs::World* world_ = nullptr;
  scene::Scene* scene_ = nullptr;
  systems::SystemGraph* systems_ = nullptr;
  renderer::GraphicsDevice* graphics_ = nullptr;
  ImGuiContext* imgui_context_ = nullptr;
  app::UITextureHandle font_texture_ = 0;
  app::UIContext* pending_ctx_ = nullptr;
  scene::NodeId selected_node_ = scene::Node::kInvalidId;
  int shadow_map_size_ = 2048;
  float shadow_bias_ = 0.0006f;
  int shadow_pcf_radius_ = 0;
  int shadow_raster_depth_bias_ = 0;
  float shadow_raster_slope_bias_ = 0.0f;
  float shadow_receiver_bias_scale_ = 0.75f;
  float shadow_normal_bias_scale_ = 1.0f;
  float point_shadow_constant_bias_ = 0.0012f;
  float point_shadow_slope_bias_scale_ = 2.0f;
  float point_shadow_normal_bias_scale_ = 1.5f;
  float point_shadow_receiver_bias_scale_ = 0.35f;
  float local_light_distance_damping_ = 0.02f;
  float local_light_range_falloff_exponent_ = 1.1f;
  bool ao_affects_local_lights_ = false;
  float local_light_directional_shadow_lift_strength_ = 0.0f;
  float lighting_exposure_ = 1.0f;
  int forward_plus_tile_size_ = 16;
  int forward_plus_max_lights_per_tile_ = 128;
  int forward_plus_max_local_lights_ = 4096;
  static constexpr size_t kFrameHistorySize = 180;
  std::array<float, kFrameHistorySize> frame_time_history_ms_{};
  size_t frame_time_history_cursor_ = 0;
  size_t frame_time_history_count_ = 0;
  uint64_t hitch_count_ = 0;
  float worst_frame_ms_ = 0.0f;
  float hitch_threshold_ms_ = 25.0f;
};

}  // namespace karma::debug
