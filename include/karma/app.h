#pragma once

#include "karma/assets.h"
#include "karma/audio.h"
#include "karma/components.h"
#include "karma/core.h"
#include "karma/math.h"
#include "karma/physics.h"
#include "karma/platform.h"
#include "karma/rendering.h"
#include "karma/scenes.h"
#include "karma/ui.h"
#include "karma/world.h"

namespace karma::visual { class LightPulseSystem; namespace particles { class ParticleSystem; } }



#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace karma::platform {
class Window;
}

namespace karma::app {

/// \ingroup karma_runtime
/// Input trigger mode for action bindings.
enum class Trigger {
  Down,
  Pressed
};

/// \ingroup karma_runtime
/// Key or mouse binding for one named action.
struct Binding {
  Trigger trigger = Trigger::Down;
  platform::Key key = platform::Key::Unknown;
  platform::MouseButton mouse = platform::MouseButton::Left;
  platform::GamepadButton gamepad_button = platform::GamepadButton::Unknown;
  platform::GamepadAxis gamepad_axis = platform::GamepadAxis::Unknown;
  float gamepad_axis_threshold = 0.5f;
  bool gamepad_axis_positive = true;
  platform::Modifiers mods{};
  bool use_key = true;
  bool use_gamepad_button = false;
  bool use_gamepad_axis = false;
};

/// Controls and whole input devices withheld from gameplay for one frame.
struct InputFilter {
  bool keyboard = false;
  bool pointer = false;
  bool gamepad = false;
  /// Suppresses gameplay mouse deltas without claiming pointer buttons.
  bool mouse_motion = false;
  std::unordered_set<platform::Key> keys;
  std::unordered_set<platform::MouseButton> mouse_buttons;
  std::unordered_set<platform::GamepadButton> gamepad_buttons;
  std::unordered_set<platform::GamepadAxis> gamepad_axes;

  [[nodiscard]] bool suppresses(platform::Key key) const {
    return keyboard || keys.contains(key);
  }
  [[nodiscard]] bool suppresses(platform::MouseButton button) const {
    return pointer || mouse_buttons.contains(button);
  }
  [[nodiscard]] bool suppresses(platform::GamepadButton button) const {
    return gamepad || gamepad_buttons.contains(button);
  }
  [[nodiscard]] bool suppresses(platform::GamepadAxis axis) const {
    return gamepad || gamepad_axes.contains(axis);
  }
};

/// \ingroup karma_runtime
/// Per-frame action and mouse input state.
///
/// Bind actions once, call `update()` with platform events each frame, then
/// query actions during gameplay and UI. Each update replaces transient state.
class InputSystem {
 public:
  /// Sets the platform window used for current key/mouse state.
  void setWindow(const platform::Window* window) {
    if (window_ != window) {
      window_ = window;
      previous_gamepad_axes_.clear();
      has_mouse_pos_ = false;
    }
  }

  /// Binds a keyboard key to an action.
  void bindKey(const std::string& action, platform::Key key, Trigger trigger = Trigger::Down);
  /// Binds a mouse button to an action.
  void bindMouse(const std::string& action, platform::MouseButton button,
                 Trigger trigger = Trigger::Down);
  /// Binds a normalized gamepad button to an action.
  void bindGamepadButton(const std::string& action,
                         platform::GamepadButton button,
                         Trigger trigger = Trigger::Down);
  /// Binds a normalized gamepad axis direction to an action.
  void bindGamepadAxis(const std::string& action,
                       platform::GamepadAxis axis,
                       float threshold = 0.5f,
                       bool positive = true,
                       Trigger trigger = Trigger::Down);
  /// Requires modifiers for an existing action binding.
  void setRequiredModifiers(const std::string& action, platform::Modifiers mods);

  /// Consumes platform events and updates action/mouse state.
  void update(const std::vector<platform::Event>& events,
              const InputFilter& filter = {});

  /// Returns true while an action is currently down.
  bool actionDown(const std::string& action) const;
  /// Returns true only on the frame an action was pressed.
  bool actionPressed(const std::string& action) const;
  /// Mouse movement delta since the previous update.
  float mouseDeltaX() const { return mouse_delta_x_; }
  /// Mouse movement delta since the previous update.
  float mouseDeltaY() const { return mouse_delta_y_; }
  /// Writes the latest mouse position if one is known.
  bool mousePosition(double& x, double& y) const {
    if (!has_mouse_pos_) {
      return false;
    }
    x = last_mouse_x_;
    y = last_mouse_y_;
    return true;
  }

  /// Clears transient per-frame state.
  void clear();

 private:
  bool matchesModifiers(const platform::Modifiers& event_mods,
                        const platform::Modifiers& required_mods) const;

  std::unordered_map<std::string, std::vector<Binding>> bindings_;
  std::unordered_set<std::string> pressed_this_frame_;
  std::unordered_set<std::string> down_this_frame_;
  std::unordered_map<int, std::unordered_map<platform::GamepadAxis, float>>
      previous_gamepad_axes_;
  const platform::Window* window_ = nullptr;
  float mouse_delta_x_ = 0.0f;
  float mouse_delta_y_ = 0.0f;
  bool has_mouse_pos_ = false;
  double last_mouse_x_ = 0.0;
  double last_mouse_y_ = 0.0;
};

}  // namespace karma::app



namespace karma::app {

/// \ingroup karma_runtime
/// Renderer texture handle used by UI draw commands.
using UITextureHandle = rendering::UITextureHandle;

/// \ingroup karma_runtime
/// UI texture descriptor returned by texture creation/loading helpers.
struct UITexture {
  UITextureHandle handle = 0;
  int width = 0;
  int height = 0;

  explicit operator bool() const { return handle != 0 && width > 0 && height > 0; }
};

/// \ingroup karma_runtime
/// Timing and viewport data supplied to UI layers.
struct UIFrameInfo {
  float dt = 0.0f;
  /// Compatibility framebuffer viewport dimensions.
  int viewport_w = 0;
  int viewport_h = 0;
  /// Compatibility X-axis scale. Prefer `scale_x` and `scale_y`.
  float dpi_scale = 1.0f;
  int logical_width = 0;
  int logical_height = 0;
  int framebuffer_width = 0;
  int framebuffer_height = 0;
  float scale_x = 1.0f;
  float scale_y = 1.0f;
};

}  // namespace karma::app


#include <filesystem>
#include <unordered_set>


namespace karma::app {
class InputSystem;
}

namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::app {

/// \ingroup karma_runtime
/// Per-frame UI bridge passed to `UiLayer` implementations.
///
/// UI providers write normalized draw lists into `drawData()`. The context also
/// owns transient UI textures and exposes input data to provider adapters.
class UIContext {
 public:
  UIContext() = default;
  ~UIContext();

  UIContext(const UIContext&) = delete;
  UIContext& operator=(const UIContext&) = delete;

  /// Frame timing and viewport data for the current UI frame.
  UIFrameInfo frame() const { return frame_; }

  /// Creates a renderer texture from RGBA8 pixels and tracks it as UI-owned.
  UITextureHandle createTextureRGBA8(int width, int height, const void* pixels);
  /// Loads a PNG file into a UI-owned RGBA8 texture. Premultiplication is
  /// available for providers such as RmlUi that require that alpha mode.
  UITexture loadTextureRGBA8FromPng(const std::filesystem::path& path,
                                    bool premultiply_alpha = false);
  /// Updates an existing RGBA8 UI texture.
  void updateTextureRGBA8(UITextureHandle texture,
                          int width,
                          int height,
                          const void* pixels);
  /// Destroys a UI texture.
  void destroyTexture(UITextureHandle texture);
  /// Destroys all textures owned by this context.
  void destroyOwnedTextures();
  /// Releases owned textures, clears draw data, and detaches frame services.
  void reset();

  /// Mutable draw list consumed by the renderer.
  rendering::UIDrawData& drawData() { return draw_data_; }

  /// Input system for UI provider adapters.
  karma::app::InputSystem& input();
  /// Replaces the platform clipboard text while the context is attached.
  void setClipboardText(std::string_view text);
  /// Returns current platform clipboard text while the context is attached.
  [[nodiscard]] std::string clipboardText() const;
  /// Requests a platform cursor shape for the current UI frame.
  void setCursorShape(platform::CursorShape shape);

 private:
  friend class EngineApp;
  UIFrameInfo frame_{};
  rendering::UIDrawData draw_data_{};
  std::unordered_set<UITextureHandle> owned_textures_;
  app::InputSystem* input_ = nullptr;
  rendering::GraphicsDevice* device_ = nullptr;
  platform::Window* window_ = nullptr;
  platform::CursorShape requested_cursor_shape_ =
      platform::CursorShape::Default;
  bool cursor_shape_requested_ = false;
};

/// Per-event result used to stop routing to lower UI layers and gameplay.
enum class UiEventDisposition : std::uint8_t {
  Ignored,
  Consumed,
};

/// Persistent whole-device capture reported by a UI layer.
struct UiInputCapture {
  bool keyboard = false;
  bool pointer = false;
  bool gamepad = false;

  [[nodiscard]] bool any() const { return keyboard || pointer || gamepad; }
};

/// \ingroup karma_runtime
/// UI provider/application layer interface.
class UiLayer {
 public:
  virtual ~UiLayer() = default;
  /// Called once per frame to populate `UIContext::drawData()`.
  virtual void onFrame(UIContext& ctx) = 0;
  /// Receives platform events before the app clears them.
  virtual UiEventDisposition onEvent(const platform::Event& event) {
    (void)event;
    return UiEventDisposition::Ignored;
  }
  /// Reports persistent capture such as focused keyboard input or pointer drag.
  [[nodiscard]] virtual UiInputCapture inputCapture() const { return {}; }
  /// Called during engine shutdown.
  virtual void onShutdown() {}
};

}  // namespace karma::app


#include <cstdint>


namespace karma::rendering {
class GraphicsDevice;
}  // namespace karma::rendering

namespace karma::assets {
class AssetRegistry;
}  // namespace karma::assets

namespace karma::world {
class Scene;
}  // namespace karma::world

namespace karma::app {

/// \ingroup karma_runtime
/// Borrowed subsystem pointers provided to runtime modules on attach.
struct RuntimeModuleContext {
  world::Scene* scene = nullptr;
  rendering::GraphicsDevice* graphics = nullptr;
  assets::AssetRegistry* assets = nullptr;
};

/// \ingroup karma_runtime
/// Optional runtime feature hook owned by `EngineApp`.
///
/// Runtime modules are the extension point for feature systems that need per
/// frame updates or renderer/resource warmup but should not be hardwired into
/// the core app startup path.
class RuntimeModule {
 public:
  virtual ~RuntimeModule() = default;

  /// Called when the module is registered with an initialized engine context.
  virtual void onAttach(const RuntimeModuleContext& context) = 0;
  /// Called before the module is destroyed or the engine shuts down.
  virtual void onDetach() {}
  /// Called during renderer warmup.
  virtual void onWarmUp(world::World& world) {
    onUpdate(world, 0.0f, 1.0f);
  }
  /// Called once near the beginning of each frame before fixed simulation steps.
  virtual void onFrameBegin(world::World& world, float dt) {
    (void)world;
    (void)dt;
  }
  /// Called before each fixed simulation step.
  virtual void onBeforeFixedUpdate(world::World& world, float fixed_dt, uint64_t fixed_tick) {
    (void)world;
    (void)fixed_dt;
    (void)fixed_tick;
  }
  /// Called after each fixed simulation step.
  virtual void onAfterFixedUpdate(world::World& world, float fixed_dt, uint64_t fixed_tick) {
    (void)world;
    (void)fixed_dt;
    (void)fixed_tick;
  }
  /// Called once per rendered frame.
  virtual void onUpdate(world::World& world, float dt, float interpolation_alpha) = 0;
  /// Called at the end of a frame after rendering/presentation work.
  virtual void onFrameEnd(world::World& world) {
    (void)world;
  }
};

}  // namespace karma::app



namespace karma::app {

/// \ingroup karma_runtime
/// Base class implemented by game/application code.
///
/// `EngineApp` binds protected subsystem pointers before `onStart()` and clears
/// ownership at shutdown. The pointers are borrowed; do not store them beyond
/// the lifetime of the game object and engine.
class GameInterface {
 public:
  virtual ~GameInterface() = default;

  /// Called once after engine subsystems are initialized.
  virtual void onStart() = 0;
  /// Called zero or more times per frame at fixed timestep.
  virtual void onFixedUpdate(float dt) = 0;
  /// Called after fixed physics/simulation work for each fixed step.
  virtual void onPostFixedUpdate(float dt) { (void)dt; }
  /// Called once per rendered frame.
  virtual void onUpdate(float dt) = 0;
  /// Called during engine shutdown.
  virtual void onShutdown() = 0;

 protected:
  /// Interpolation alpha for rendering between fixed simulation steps.
  float renderInterpolationAlpha() const { return render_interpolation_alpha_; }
  /// Borrowed ECS world.
  world::World* world = nullptr;
  /// Borrowed scene hierarchy.
  world::Scene* scene = nullptr;
  /// Borrowed input system.
  app::InputSystem* input = nullptr;
  /// Borrowed physics world.
  physics::World* physics = nullptr;
  /// Borrowed graphics device. Null in headless builds.
  rendering::GraphicsDevice* graphics = nullptr;
  /// Borrowed render system. Null in headless builds.
  rendering::RenderSystem* renderer = nullptr;
  /// Borrowed normalized runtime asset registry.
  assets::AssetRegistry* assets = nullptr;
  /// Borrowed optional system graph.
  world::SystemGraph* systems = nullptr;
  /// Borrowed first-party UI system. Null in headless or disabled profiles.
  ui::System* ui = nullptr;

 private:
  friend class EngineApp;
  void bindContext(world::World& world, world::Scene& scene, app::InputSystem& input,
                   physics::World& physics, rendering::GraphicsDevice* graphics,
                   rendering::RenderSystem* renderer,
                   assets::AssetRegistry& assets,
                   world::SystemGraph& systems,
                   ui::System* native_ui) {
    this->world = &world;
    this->scene = &scene;
    this->input = &input;
    this->physics = &physics;
    this->graphics = graphics;
    this->renderer = renderer;
    this->assets = &assets;
    this->systems = &systems;
    this->ui = native_ui;
  }

  void unbindContext() {
    world = nullptr;
    scene = nullptr;
    input = nullptr;
    physics = nullptr;
    graphics = nullptr;
    renderer = nullptr;
    assets = nullptr;
    systems = nullptr;
    ui = nullptr;
    render_interpolation_alpha_ = 1.0f;
  }

  float render_interpolation_alpha_ = 1.0f;
};

}  // namespace karma::app


#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>


struct ImGuiContext;

namespace karma::world {
class World;
}

namespace karma::world {
class Scene;
}

namespace karma::assets {
class AssetRegistry;
}

namespace karma::rendering {
class GraphicsDevice;
}

namespace karma::world {
class SystemGraph;
}

namespace karma::app {

/// \ingroup karma_runtime
/// Built-in ImGui debug overlay for scene, renderer, and frame diagnostics.
///
/// This layer is compiled when `KARMA_BUILD_DEBUG_UI` is enabled. It uses the
/// same `UiLayer` contract as application UI and should remain optional.
class DebugOverlayLayer final : public app::UiLayer {
 public:
  DebugOverlayLayer(world::World* world,
                    world::Scene* scene,
                    world::SystemGraph* systems,
                    rendering::GraphicsDevice* graphics,
                    assets::AssetRegistry* assets,
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

  UiEventDisposition onEvent(const platform::Event& event) override;
  [[nodiscard]] UiInputCapture inputCapture() const override;
  void onFrame(app::UIContext& ctx) override;
  void onShutdown() override;

 private:
  void drawDebugWindow(float frame_ms, float framerate);
  void drawSceneTab();
  void drawSceneHierarchyPane();
  void drawSelectedInspectorPane();
  void drawSelectedSummary(const world::Node& node);
  void drawComponentInspector(const world::Node& node);
  void drawRendererTab();
  void drawParticlesTab();
  void drawPerformanceTab(float frame_ms, float framerate);
  void resetFramePacingStats();

  world::World* world_ = nullptr;
  world::Scene* scene_ = nullptr;
  world::SystemGraph* systems_ = nullptr;
  rendering::GraphicsDevice* graphics_ = nullptr;
  assets::AssetRegistry* assets_ = nullptr;
  ImGuiContext* imgui_context_ = nullptr;
  app::UITextureHandle font_texture_ = 0;
  app::UIContext* pending_ctx_ = nullptr;
  world::NodeId selected_node_ = world::Node::kInvalidId;
  std::string hierarchy_filter_;
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
  bool render_fps_initialized_ = false;
  uint64_t render_fps_last_completed_frames_ = 0;
  std::chrono::steady_clock::time_point render_fps_last_sample_{};
  float render_completed_fps_ = 0.0f;
};

}  // namespace karma::app


#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>


namespace karma::platform {
class Window;
}

namespace karma::visual {
class LightPulseSystem;
namespace particles {
class ParticleSystem;
}  // namespace particles
}  // namespace karma::visual

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
  ui::UiSystemConfig native_ui{};
  float fixed_dt = 1.0f / 60.0f;
  float max_frame_dt = 0.25f;
  // Default to the low-latency present path. Set true to force vblank pacing.
  bool vsync = false;
  /// Optional explicit swapchain present mode. `Auto` uses the `vsync` policy.
  rendering::PresentMode present_mode = rendering::PresentMode::Auto;
  /// Renderer execution ownership. Threaded keeps backend present off the game thread.
  rendering::RendererExecutionMode renderer_execution_mode =
      rendering::RendererExecutionMode::Threaded;
  /// CPU-side frame-start cap in frames per second. Defaults to 60 FPS for normal apps.
  /// Set to 0 to disable CPU pacing for profiling or custom loops. This is separate
  /// from swapchain present/vsync policy; use `vsync` or `present_mode` for that.
  float frame_pacing_fps = 60.0f;
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
  math::Color background_color{0.0f, 0.0f, 0.0f, 1.0f};
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
  /// Startup default renderer frame graph used by cameras with empty keys.
  rendering::FrameGraphDesc default_frame_graph{};
  /// Asset packages imported before `GameInterface::onStart`.
  std::vector<std::filesystem::path> startup_asset_packages;
  /// Scene asset keys instantiated after startup packages and before `GameInterface::onStart`.
  std::vector<std::string> startup_scene_assets;
  /// Prewarm startup packages after initial content and systems are committed.
  bool prewarm_startup_packages = true;
};

/// \ingroup karma_runtime
/// Result of validating engine startup configuration before resources are created.
struct EngineConfigValidation {
  std::vector<std::string> errors;

  bool valid() const { return errors.empty(); }
  explicit operator bool() const { return valid(); }
};

/// Validates startup values that would otherwise cause invalid resources or
/// non-terminating fixed-step updates.
[[nodiscard]] EngineConfigValidation validateEngineConfig(const EngineConfig& config);

/// \ingroup karma_runtime
/// Application shell that owns and wires core engine subsystems.
///
/// `EngineApp` creates the window, graphics device, render system, particles,
/// animation, physics/collision, audio, input, UI contexts, and optional runtime
/// modules. Game code usually subclasses `GameInterface`, optionally registers
/// feature modules, then calls `start()` and repeatedly calls `tick()`.
/// Each `EngineApp` owns one start/shutdown lifecycle; create a new instance to
/// run another game session.
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
  /// Returns the owned first-party UI system, or null when unavailable/disabled.
  [[nodiscard]] ui::System* nativeUi() const;
  /// Shows or hides the platform cursor.
  void setCursorVisible(bool visible);
  /// Registers an optional runtime feature module.
  ///
  /// Modules registered before `start()` participate in attach and warmup.
  /// Modules registered after startup are attached immediately and begin frame
  /// callbacks on the next tick.
  void addRuntimeModule(std::unique_ptr<RuntimeModule> module);

 private:
  void initSubsystems();
  void shutdownSubsystems();
  void shutdownRunningGame();
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
  app::InputSystem input_;
  std::unique_ptr<rendering::GraphicsDevice> graphics_;
  std::unique_ptr<rendering::RenderSystem> render_system_;
  std::unique_ptr<visual::particles::ParticleSystem> particle_system_;
  world::AnimationSystem animation_system_;
  world::DeformationSystem deformation_system_;
  std::unique_ptr<visual::LightPulseSystem> light_pulse_system_;
  audio::Audio audio_;
  std::unique_ptr<audio::AudioSystem> audio_system_;
  physics::World physics_;
  world::World world_;
  world::Scene scene_;
  assets::AssetRegistry assets_;
  std::vector<assets::AssetPackageHandle> startup_asset_package_handles_;
  std::vector<scenes::SceneInstantiateResult> startup_scene_results_;
  rendering::RenderPrewarmHandle startup_prewarm_handle_{};
  rendering::TextureId loading_splash_texture_ = rendering::kInvalidTexture;
  int loading_splash_texture_width_ = 0;
  int loading_splash_texture_height_ = 0;
  std::mutex loading_splash_graphics_mutex_;
  bool loading_splash_presented_ = false;
  world::SystemGraph systems_;
  std::vector<std::unique_ptr<RuntimeModule>> runtime_modules_;
  std::unordered_set<RuntimeModule*> attached_runtime_modules_;
  EngineConfig config_{};
#if !defined(KARMA_HEADLESS) && defined(KARMA_ENABLE_NATIVE_UI)
  std::unique_ptr<ui::System> native_ui_;
  rendering::UIDrawData native_ui_draw_data_{};
#endif
  std::unique_ptr<UiLayer> user_ui_;
#if defined(KARMA_DEBUG_UI)
  std::unique_ptr<UiLayer> debug_ui_;
  UIContext debug_ui_context_{};
#endif
  UIContext user_ui_context_{};
  InputFilter ui_input_filter_{};
  bool debug_ui_enabled_ = false;
  uint64_t last_synced_entity_version_ = std::numeric_limits<uint64_t>::max();

  bool running_ = false;
  bool has_started_ = false;
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
