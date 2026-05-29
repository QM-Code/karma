#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include "karma/app/game_interface.h"
#include "karma/app/runtime_module.h"
#include "karma/animation/animation_system.h"
#include "karma/animation/cpu_skinning_system.h"
#include "karma/ecs/world.h"
#include "karma/input/input_system.h"
#include "karma/audio/audio.h"
#include "karma/audio/audio_system.h"
#include "karma/platform/window.h"
#include "karma/particles/effect_library.h"
#include "karma/particles/particle_system.h"
#include "karma/prefabs/prefab_system.h"
#include "karma/prefabs/prefab_registry.h"
#include "karma/app/ui_context.h"
#include "karma/physics/physics_world.hpp"
#include "karma/physics/physics_system.h"
#include "karma/renderer/device.h"
#include "karma/renderer/material_library.h"
#include "karma/renderer/render_system.h"
#include "karma/scene/scene.h"
#include "karma/systems/system_graph.h"

namespace karma::platform {
class Window;
}

namespace karma::app {

struct EngineConfig {
  platform::WindowConfig window{};
  float fixed_dt = 1.0f / 60.0f;
  float max_frame_dt = 0.25f;
  bool vsync = true;
  bool fullscreen = false;
  bool cursor_visible = true;
  std::filesystem::path environment_map;
  float environment_intensity = 0.0f;
  bool environment_draw_skybox = true;
  bool enable_anisotropy = false;
  int anisotropy_level = 1;
  bool generate_mipmaps = false;
  int forward_plus_tile_size = 16;
  int forward_plus_max_lights_per_tile = 128;
  int forward_plus_max_local_lights = 4096;
  int shadow_map_size = 2048;
  float shadow_bias = 0.0006f;
  int shadow_pcf_radius = 0;
  int shadow_raster_depth_bias = 0;
  float shadow_raster_slope_bias = 0.0f;
  float shadow_receiver_bias_scale = 0.75f;
  float shadow_normal_bias_scale = 1.0f;
  float point_shadow_constant_bias = 0.0012f;
  float point_shadow_slope_bias_scale = 2.0f;
  float point_shadow_normal_bias_scale = 1.5f;
  float point_shadow_receiver_bias_scale = 0.35f;
  int point_shadow_max_lights = 2;
  float local_light_distance_damping = 0.02f;
  float local_light_range_falloff_exponent = 1.1f;
  bool ao_affects_local_lights = false;
  float local_light_directional_shadow_lift_strength = 0.0f;
  float lighting_exposure = 1.0f;
};

class EngineApp {
 public:
  EngineApp();
  ~EngineApp();

  EngineApp(const EngineApp&) = delete;
  EngineApp& operator=(const EngineApp&) = delete;

  void start(GameInterface& game, const EngineConfig& config = {});
  void tick();
  bool isRunning() const { return running_; }
  void requestStop();
  void setUi(std::unique_ptr<UiLayer> ui);
  void setCursorVisible(bool visible);
  void addRuntimeModule(std::unique_ptr<RuntimeModule> module);

 private:
  void initSubsystems();
  void shutdownSubsystems();
  void syncSceneEntities();
  void warmUpRenderer();
  RuntimeModuleContext makeRuntimeModuleContext();
#if defined(KARMA_DEBUG_UI)
  std::unique_ptr<UiLayer> createDebugOverlayUi();
#endif

  GameInterface* game_ = nullptr;
  std::unique_ptr<platform::Window> window_;
  input::InputSystem input_;
  std::unique_ptr<renderer::GraphicsDevice> graphics_;
  std::unique_ptr<renderer::RenderSystem> render_system_;
  std::unique_ptr<prefabs::PrefabSystem> prefab_system_;
  std::unique_ptr<prefabs::PrefabRegistry> prefab_registry_;
  std::unique_ptr<particles::ParticleSystem> particle_system_;
  animation::AnimationSystem animation_system_;
  animation::CpuSkinningSystem cpu_skinning_system_;
  audio::Audio audio_;
  std::unique_ptr<audio::AudioSystem> audio_system_;
  physics::World physics_;
  ecs::World world_;
  scene::Scene scene_;
  renderer::MaterialLibrary materials_;
  particles::ParticleLibrary particle_effects_;
  systems::SystemGraph systems_;
  std::vector<std::unique_ptr<RuntimeModule>> runtime_modules_;
  EngineConfig config_{};
  std::unique_ptr<UiLayer> user_ui_;
#if defined(KARMA_DEBUG_UI)
  std::unique_ptr<UiLayer> debug_ui_;
  UIContext debug_ui_context_{};
#endif
  UIContext user_ui_context_{};
  bool debug_ui_enabled_ = false;
  uint64_t last_synced_entity_version_ = std::numeric_limits<uint64_t>::max();

  bool running_ = false;
  float fixed_dt_ = 1.0f / 60.0f;
  float accumulator_ = 0.0f;
  std::chrono::steady_clock::time_point last_time_{};
};

}  // namespace karma::app
