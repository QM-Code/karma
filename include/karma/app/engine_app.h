#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

#include "karma/app/game_interface.h"
#include "karma/ecs/world.h"
#include "karma/input/input_system.h"
#include "karma/audio/audio.h"
#include "karma/audio/audio_system.h"
#include "karma/platform/window.h"
#include "karma/app/ui_context.h"
#include "karma/physics/physics_world.hpp"
#include "karma/physics/physics_system.h"
#include "karma/renderer/device.h"
#include "karma/renderer/render_system.h"
#include "karma/scene/scene.h"
#include "karma/systems/system_graph.h"
#include <unordered_map>

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
  int shadow_map_size = 2048;
  float shadow_bias = 0.0006f;
  int shadow_pcf_radius = 0;
  int shadow_raster_depth_bias = 0;
  float shadow_raster_slope_bias = 0.0f;
  float shadow_receiver_bias_scale = 0.75f;
  float shadow_normal_bias_scale = 1.0f;
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

 private:
 void initSubsystems();
 void shutdownSubsystems();
 void syncSceneEntities();

  GameInterface* game_ = nullptr;
  std::unique_ptr<platform::Window> window_;
  input::InputSystem input_;
  std::unique_ptr<renderer::GraphicsDevice> graphics_;
  std::unique_ptr<renderer::RenderSystem> render_system_;
  audio::Audio audio_;
  std::unique_ptr<audio::AudioSystem> audio_system_;
  physics::World physics_;
  ecs::World world_;
  scene::Scene scene_;
  systems::SystemGraph systems_;
  EngineConfig config_{};
  std::unique_ptr<UiLayer> ui_;
  UIContext ui_context_{};
  bool debug_ui_enabled_ = false;
  std::unordered_map<uint64_t, scene::NodeId> entity_nodes_;

  bool running_ = false;
  float fixed_dt_ = 1.0f / 60.0f;
  float accumulator_ = 0.0f;
  std::chrono::steady_clock::time_point last_time_{};
};

}  // namespace karma::app
