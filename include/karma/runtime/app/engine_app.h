#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/content/assets/asset_package.h"
#include "karma/content/assets/asset_registry.h"
#include "karma/runtime/app/game_interface.h"
#include "karma/runtime/app/runtime_module.h"
#include "karma/core/time.h"
#include "karma/simulation/animation/animation_system.h"
#include "karma/simulation/animation/deformation_system.h"
#include "karma/world/ecs/world.h"
#include "karma/runtime/input/input_system.h"
#include "karma/media/audio/audio.h"
#include "karma/media/audio/audio_system.h"
#include "karma/platform/window/window.h"
#include "karma/features/visual/lights/light_pulse_system.h"
#include "karma/features/visual/particles/particle_system.h"
#include "karma/runtime/app/ui_context.h"
#include "karma/simulation/physics/physics_world.hpp"
#include "karma/simulation/physics/physics_system.h"
#include "karma/rendering/renderer/device.h"
#include "karma/rendering/renderer/post_process.h"
#include "karma/rendering/renderer/render_system.h"
#include "karma/world/scene/scene.h"
#include "karma/world/systems/system_graph.h"

namespace karma::platform {
class Window;
}

namespace karma::app {

/// \ingroup karma_runtime
/// Engine startup and renderer/simulation tuning.
///
/// `EngineConfig` is passed once to `EngineApp::start(...)`. Runtime debug UI
/// can expose some renderer fields for live tuning, but this struct is the
/// canonical startup configuration.
struct EngineConfig {
  struct LoadingSplashConfig {
    bool enabled = true;
    int target_fps = 30;
    /// Delay the first splash frame so fast warm starts do not flash it.
    /// Set to 0 to show the splash immediately.
    int show_after_ms = 750;
    std::filesystem::path image_path{"docs/logo.png"};
    math::Color background{0.0f, 0.0f, 0.0f, 1.0f};
    math::Color accent{0.24f, 0.56f, 1.0f, 1.0f};
    math::Color foreground{1.0f, 1.0f, 1.0f, 1.0f};
  };

  platform::WindowConfig window{};
  LoadingSplashConfig loading_splash{};
  float fixed_dt = 1.0f / 60.0f;
  float max_frame_dt = 0.25f;
  // Default to the low-latency present path. In the Diligent Vulkan backend this uses
  // Present(0), where Diligent prefers MAILBOX, then IMMEDIATE, then FIFO if neither
  // low-latency mode is supported. Set true to force FIFO/FIFO_RELAXED vblank pacing.
  bool vsync = false;
  /// Optional explicit swapchain present mode. `Auto` uses the `vsync` policy.
  renderer::PresentMode present_mode = renderer::PresentMode::Auto;
  /// Renderer execution ownership. Threaded keeps backend present off the game thread.
  renderer::RendererExecutionMode renderer_execution_mode =
      renderer::RendererExecutionMode::Threaded;
  /// Optional CPU-side frame-start cap. 0 disables it. This is useful with low-latency
  /// present modes that can otherwise run into swapchain acquire/present pacing stalls.
  float frame_pacing_fps = 0.0f;
  /// Skips swapchain present on frames that consumed mouse-button events. This is
  /// an opt-in workaround for Linux compositor/driver present stalls on clicks.
  bool skip_present_on_mouse_button = false;
  /// Number of game frames to skip after a mouse-button event when the workaround
  /// above is enabled. The event frame counts as the first frame.
  uint32_t mouse_button_present_skip_frames = 2;
  /// Extra startup warm-up frames that rotate the primary camera around yaw.
  /// Useful for examples that show driver hitches when first looking at new views.
  uint32_t renderer_warmup_camera_sweep_steps = 0;
  bool fullscreen = false;
  bool cursor_visible = true;
  /// Source path imported as the startup default environment map asset.
  std::filesystem::path environment_map_source_path;
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
  /// Startup default post-process profile used by cameras with empty keys.
  renderer::PostProcessSettings post_process{};
  /// Asset packages imported before `GameInterface::onStart`.
  std::vector<std::filesystem::path> startup_asset_packages;
  /// Prewarm startup packages after initial content and systems are committed.
  bool prewarm_startup_packages = true;
};

/// \ingroup karma_runtime
/// Application shell that owns and wires core engine subsystems.
///
/// `EngineApp` creates the window, graphics device, render system, particles,
/// animation, physics/collision, audio, input, UI contexts, and optional runtime
/// modules. Game code usually subclasses `GameInterface`, optionally registers
/// feature modules, then calls `start()` and repeatedly calls `tick()`.
///
/// \lifetime `EngineApp` must outlive the bound `GameInterface` and any UI or
/// runtime modules registered into it.
class EngineApp {
 public:
  EngineApp();
  ~EngineApp();

  EngineApp(const EngineApp&) = delete;
  EngineApp& operator=(const EngineApp&) = delete;

  /// Starts the engine and calls `game.onStart()` after subsystems are ready.
  void start(GameInterface& game, const EngineConfig& config = {});
  /// Runs one frame: input/events, fixed updates, systems, render, UI, present.
  void tick();
  /// Returns true while the window/app is still running.
  bool isRunning() const { return running_; }
  /// Requests a graceful shutdown at the next safe point.
  void requestStop();
  /// Sets the user UI layer. Can be called before startup.
  void setUi(std::unique_ptr<UiLayer> ui);
  /// Shows or hides the platform cursor.
  void setCursorVisible(bool visible);
  /// Registers an optional runtime feature module.
  ///
  /// Modules registered before `start()` participate in attach and warmup.
  /// Modules registered after startup are attached immediately.
  void addRuntimeModule(std::unique_ptr<RuntimeModule> module);

 private:
  void initSubsystems();
  void shutdownSubsystems();
  void syncSceneEntities();
  bool ensureLoadingSplashTexture();
  void releaseLoadingSplashTexture();
  bool renderLoadingSplash(float progress);
  bool shouldRenderLoadingSplash(core::SteadyClock::time_point startup_start) const;
  bool renderLoadingSplashIfDue(float progress, core::SteadyClock::time_point startup_start);
  bool presentInitialLoadingSplash(float progress,
                                   core::SteadyClock::time_point startup_start);
  void warmUpRenderer();
  double applyFramePacing();
  RuntimeModuleContext makeRuntimeModuleContext();
#if defined(KARMA_DEBUG_UI)
  std::unique_ptr<UiLayer> createDebugOverlayUi();
#endif

  GameInterface* game_ = nullptr;
  std::unique_ptr<platform::Window> window_;
  input::InputSystem input_;
  std::unique_ptr<renderer::GraphicsDevice> graphics_;
  std::unique_ptr<renderer::RenderSystem> render_system_;
  std::unique_ptr<particles::ParticleSystem> particle_system_;
  animation::AnimationSystem animation_system_;
  animation::DeformationSystem deformation_system_;
  visual::LightPulseSystem light_pulse_system_;
  audio::Audio audio_;
  std::unique_ptr<audio::AudioSystem> audio_system_;
  physics::World physics_;
  ecs::World world_;
  scene::Scene scene_;
  content::AssetRegistry assets_;
  std::vector<content::AssetPackageHandle> startup_asset_package_handles_;
  renderer::RenderPrewarmHandle startup_prewarm_handle_{};
  renderer::TextureId loading_splash_texture_ = renderer::kInvalidTexture;
  int loading_splash_texture_width_ = 0;
  int loading_splash_texture_height_ = 0;
  std::mutex loading_splash_graphics_mutex_;
  bool loading_splash_presented_ = false;
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
  bool frame_diag_initialized_ = false;
  bool frame_diag_enabled_ = false;
  float frame_diag_threshold_ms_ = 25.0f;
  float fixed_dt_ = 1.0f / 60.0f;
  float accumulator_ = 0.0f;
  uint64_t fixed_tick_ = 0;
  uint64_t frame_tick_ = 0;
  uint64_t last_mouse_button_frame_tick_ = std::numeric_limits<uint64_t>::max();
  core::SteadyClock::time_point last_time_{};
  core::SteadyClock::time_point next_frame_pace_time_{};
};

}  // namespace karma::app
