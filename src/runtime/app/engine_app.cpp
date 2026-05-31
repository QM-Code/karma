#include "karma/runtime/app/engine_app.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include <spdlog/spdlog.h>

#include "karma/simulation/collision/collision_event_system.h"
#include "karma/runtime/debug/debug_overlay.h"
#include "karma/world/scene/transform_hierarchy.h"

namespace karma::app {
namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

float envFloat(const char* value, float fallback) {
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  try {
    return std::stof(value);
  } catch (const std::exception&) {
    return fallback;
  }
}

}  // namespace

EngineApp::EngineApp() = default;

namespace {

double elapsedMilliseconds(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start)
      .count();
}

}  // namespace

#if defined(KARMA_DEBUG_UI)
std::unique_ptr<UiLayer> EngineApp::createDebugOverlayUi() {
  if (!debug_ui_enabled_) {
    return nullptr;
  }
  return std::make_unique<debug::DebugOverlayLayer>(&world_,
                                                    &scene_,
                                                    &systems_,
                                                    graphics_.get(),
                                                    config_.shadow_map_size,
                                                    config_.shadow_bias,
                                                    config_.shadow_pcf_radius,
                                                    config_.shadow_raster_depth_bias,
                                                    config_.shadow_raster_slope_bias,
                                                    config_.shadow_receiver_bias_scale,
                                                    config_.shadow_normal_bias_scale,
                                                    config_.point_shadow_constant_bias,
                                                    config_.point_shadow_slope_bias_scale,
                                                    config_.point_shadow_normal_bias_scale,
                                                    config_.point_shadow_receiver_bias_scale,
                                                    config_.local_light_distance_damping,
                                                    config_.local_light_range_falloff_exponent,
                                                    config_.ao_affects_local_lights,
                                                    config_.local_light_directional_shadow_lift_strength,
                                                    config_.lighting_exposure,
                                                    config_.forward_plus_max_local_lights);
}
#endif

EngineApp::~EngineApp() {
  if (game_ && running_) {
    game_->onShutdown();
  }
  shutdownSubsystems();
}

RuntimeModuleContext EngineApp::makeRuntimeModuleContext() {
  return RuntimeModuleContext{
      .scene = &scene_,
      .graphics = graphics_.get(),
      .materials = &materials_,
      .particle_effects = &particle_effects_,
      .prefab_registry = prefab_registry_.get(),
  };
}

void EngineApp::addRuntimeModule(std::unique_ptr<RuntimeModule> module) {
  if (!module) {
    return;
  }
  if (running_) {
    module->onAttach(makeRuntimeModuleContext());
  }
  runtime_modules_.push_back(std::move(module));
}

void EngineApp::initSubsystems() {
  window_ = platform::CreateWindow(config_.window);
  if (window_) {
    window_->setVsync(config_.vsync);
    window_->setFullscreen(config_.fullscreen);
    window_->setCursorVisible(config_.cursor_visible);
    if (!config_.window.icon_path.empty()) {
      window_->setIcon(config_.window.icon_path);
    }
  }

  input_.setWindow(window_.get());
  prefab_system_ = std::make_unique<prefabs::PrefabSystem>();
  prefab_registry_ = std::make_unique<prefabs::PrefabRegistry>();

  if (window_) {
    graphics_ = std::make_unique<renderer::GraphicsDevice>(*window_);
    graphics_->setVsync(config_.vsync);
    render_system_ = std::make_unique<renderer::RenderSystem>(*graphics_, materials_);
    particle_system_ =
        std::make_unique<particles::ParticleSystem>(graphics_.get(), &particle_effects_);
  }

  const auto physics_system_id = systems_.addSystem(std::make_unique<physics::PhysicsSystem>(physics_));
  const auto collision_system_id =
      systems_.addSystem(std::make_unique<collision::CollisionEventSystem>());
  systems_.addDependency(collision_system_id, physics_system_id);
  audio_system_ = std::make_unique<audio::AudioSystem>(audio_);
  // Register other systems here (PhysicsSystem, AudioSystem, etc.).
}

void EngineApp::warmUpRenderer() {
  if (!graphics_ || !render_system_) {
    return;
  }

  const auto warmup_start = std::chrono::steady_clock::now();
  syncSceneEntities();
  int fb_width = 0;
  int fb_height = 0;
  if (window_) {
    window_->getFramebufferSize(fb_width, fb_height);
  }
  if (fb_width <= 0 || fb_height <= 0) {
    spdlog::info("Renderer warm-up skipped: framebuffer={}x{}", fb_width, fb_height);
    return;
  }

  renderer::FrameInfo frame{};
  frame.width = fb_width;
  frame.height = fb_height;
  frame.delta_time = 0.0f;
  graphics_->beginFrame(frame);
  if (prefab_system_) {
    prefab_system_->update(world_, 0.0f, 1.0f);
  }
  animation_system_.update(world_, scene_, 0.0f);
  scene::updateWorldTransforms(world_, scene_);
  cpu_skinning_system_.update(world_, *graphics_);
  if (particle_system_) {
    particle_system_->update(world_, 0.0f, 1.0f);
  }
  for (auto& module : runtime_modules_) {
    if (module) {
      module->onWarmUp(world_);
    }
  }
  render_system_->update(world_, scene_, 0.0f, 1.0f);
  graphics_->renderLayer(0);
  graphics_->endFrame();
  spdlog::info("Renderer warm-up took {:.2f} ms", elapsedMilliseconds(warmup_start));
}

void EngineApp::shutdownSubsystems() {
  if (user_ui_) {
    user_ui_->onShutdown();
    user_ui_.reset();
  }
#if defined(KARMA_DEBUG_UI)
  if (debug_ui_) {
    debug_ui_->onShutdown();
    debug_ui_.reset();
  }
  debug_ui_context_ = {};
#endif
  user_ui_context_ = {};
  render_system_.reset();
  prefab_system_.reset();
  if (prefab_registry_) {
    prefab_registry_->shutdown();
    prefab_registry_->clearContext();
  }
  prefab_registry_.reset();
  particle_system_.reset();
  for (auto& module : runtime_modules_) {
    if (module) {
      module->onDetach();
    }
  }
  graphics_.reset();
  window_.reset();
  running_ = false;
}

void EngineApp::setUi(std::unique_ptr<UiLayer> ui) {
  if (user_ui_) {
    user_ui_->onShutdown();
  }
  user_ui_ = std::move(ui);
}

void EngineApp::setCursorVisible(bool visible) {
  if (window_) {
    window_->setCursorVisible(visible);
  }
  config_.cursor_visible = visible;
}

void EngineApp::start(GameInterface& game, const EngineConfig& config) {
  if (running_) {
    return;
  }
  spdlog::set_level(spdlog::level::trace);
  config_ = config;
  if (const char* vsync_env = std::getenv("KARMA_ENGINE_VSYNC")) {
    config_.vsync = envFlagEnabled(vsync_env);
    spdlog::info("KARMA_ENGINE_VSYNC override: {}", config_.vsync ? "on" : "off");
  }
  fixed_dt_ = config_.fixed_dt;
  const char* debug_env = std::getenv("KARMA_ENGINE_EDITOR_DEBUG");
  debug_ui_enabled_ = debug_env && std::string(debug_env) != "0";
  initSubsystems();
#if defined(KARMA_DEBUG_UI)
  debug_ui_ = createDebugOverlayUi();
#endif
  if (graphics_) {
    graphics_->setGenerateMips(config_.generate_mipmaps);
    graphics_->setAnisotropy(config_.enable_anisotropy, config_.anisotropy_level);
    graphics_->setForwardPlusSettings(config_.forward_plus_tile_size,
                                      config_.forward_plus_max_lights_per_tile,
                                      config_.forward_plus_max_local_lights);
    graphics_->setShadowSettings(config_.shadow_bias,
                                 config_.shadow_map_size,
                                 config_.shadow_pcf_radius,
                                 config_.shadow_raster_depth_bias,
                                 config_.shadow_raster_slope_bias,
                                 config_.shadow_receiver_bias_scale,
                                 config_.shadow_normal_bias_scale);
    graphics_->setPointShadowLightLimit(config_.point_shadow_max_lights);
    graphics_->setPointShadowSettings(config_.point_shadow_constant_bias,
                                      config_.point_shadow_slope_bias_scale,
                                      config_.point_shadow_normal_bias_scale,
                                      config_.point_shadow_receiver_bias_scale);
    graphics_->setLocalLightingSettings(config_.local_light_distance_damping,
                                        config_.local_light_range_falloff_exponent,
                                        config_.ao_affects_local_lights,
                                        config_.local_light_directional_shadow_lift_strength);
    graphics_->setExposure(config_.lighting_exposure);
  }
  game_ = &game;
  running_ = true;
  accumulator_ = 0.0f;
  last_synced_entity_version_ = std::numeric_limits<uint64_t>::max();
  game_->bindContext(world_,
                     scene_,
                     input_,
                     physics_,
                     graphics_.get(),
                     materials_,
                     particle_effects_,
                     *prefab_registry_);
  if (prefab_registry_) {
    prefab_registry_->bindContext(prefabs::PrefabPackageContext{
        .graphics = graphics_.get(),
        .materials = &materials_,
        .particle_effects = &particle_effects_,
    });
  }
  for (auto& module : runtime_modules_) {
    if (module) {
      module->onAttach(makeRuntimeModuleContext());
    }
  }
  game_->onStart();
  if (graphics_) {
    const auto environment_start = std::chrono::steady_clock::now();
    graphics_->setEnvironmentMap(config_.environment_map,
                                 config_.environment_intensity,
                                 config_.environment_draw_skybox);
    spdlog::info("Engine environment setup took {:.2f} ms",
                 elapsedMilliseconds(environment_start));
  }
  warmUpRenderer();
  accumulator_ = 0.0f;
  last_time_ = std::chrono::steady_clock::now();
}

void EngineApp::requestStop() {
  running_ = false;
}

void EngineApp::syncSceneEntities() {
  const uint64_t entity_version = world_.entityVersion();
  if (entity_version == last_synced_entity_version_) {
    return;
  }

  for (const ecs::Entity entity : world_.entities()) {
    if (scene_.findNode(entity) == scene::Node::kInvalidId) {
      scene_.createNode(entity);
    }
  }

  std::vector<scene::NodeId> stale_nodes;
  for (const auto& node : scene_.nodes()) {
    if (!scene_.isAlive(node.id) || !node.entity.isValid()) {
      continue;
    }
    if (!world_.isAlive(node.entity)) {
      stale_nodes.push_back(node.id);
    }
  }
  for (const scene::NodeId id : stale_nodes) {
    scene_.destroyNode(id);
  }
  last_synced_entity_version_ = entity_version;
}

void EngineApp::tick() {
  if (!running_ || !game_) {
    return;
  }

  if (!frame_diag_initialized_) {
    frame_diag_initialized_ = true;
    frame_diag_enabled_ = envFlagEnabled(std::getenv("KARMA_ENGINE_FRAME_DIAG"));
    frame_diag_threshold_ms_ =
        std::max(0.0f, envFloat(std::getenv("KARMA_ENGINE_FRAME_DIAG_THRESHOLD_MS"),
                                frame_diag_threshold_ms_));
    if (frame_diag_enabled_) {
      spdlog::info("KARMA_ENGINE_FRAME_DIAG enabled; logging frames >= {:.2f} ms",
                   frame_diag_threshold_ms_);
    }
  }

  const auto tick_start = Clock::now();
  const auto now = std::chrono::steady_clock::now();
  const float raw_frame_dt = std::chrono::duration<float>(now - last_time_).count();
  float frame_dt = raw_frame_dt;
  if (frame_dt > config_.max_frame_dt) {
    frame_dt = config_.max_frame_dt;
  }
  last_time_ = now;
  accumulator_ += frame_dt;

  auto section_start = Clock::now();
  if (window_) {
    window_->pollEvents();
    for (const auto& event : window_->events()) {
      if (user_ui_) {
        user_ui_->onEvent(event);
      }
#if defined(KARMA_DEBUG_UI)
      if (debug_ui_) {
        debug_ui_->onEvent(event);
      }
#endif
    }
    input_.update(window_->events());
    window_->clearEvents();
    if (window_->shouldClose()) {
      requestStop();
    }
  }
  auto section_end = Clock::now();
  const double events_ms = elapsedMs(section_start, section_end);

  if (!running_) {
    if (game_) {
      game_->onShutdown();
    }
    shutdownSubsystems();
    game_ = nullptr;
    return;
  }

  int fixed_steps = 0;
  section_start = section_end;
  while (accumulator_ >= fixed_dt_) {
    game_->onFixedUpdate(fixed_dt_);
    // Physics runs via SystemGraph.
    systems_.update(world_, fixed_dt_);
    game_->onPostFixedUpdate(fixed_dt_);
    accumulator_ -= fixed_dt_;
    ++fixed_steps;
  }
  section_end = Clock::now();
  const double fixed_ms = elapsedMs(section_start, section_end);

  float render_alpha = 1.0f;
  if (fixed_dt_ > 0.0f) {
    render_alpha = std::clamp(accumulator_ / fixed_dt_, 0.0f, 1.0f);
  }
  game_->render_interpolation_alpha_ = render_alpha;

  section_start = section_end;
  game_->onUpdate(frame_dt);
  section_end = Clock::now();
  const double game_update_ms = elapsedMs(section_start, section_end);

  section_start = section_end;
  if (prefab_system_) {
    prefab_system_->update(world_, frame_dt, render_alpha);
  }
  section_end = Clock::now();
  const double prefab_ms = elapsedMs(section_start, section_end);

  section_start = section_end;
  syncSceneEntities();
  section_end = Clock::now();
  const double sync_scene_ms = elapsedMs(section_start, section_end);

  section_start = section_end;
  animation_system_.update(world_, scene_, frame_dt);
  section_end = Clock::now();
  const double animation_ms = elapsedMs(section_start, section_end);

  section_start = section_end;
  scene::updateWorldTransforms(world_, scene_);
  section_end = Clock::now();
  const double scene_transforms_ms = elapsedMs(section_start, section_end);

  section_start = section_end;
  if (graphics_) {
    cpu_skinning_system_.update(world_, *graphics_);
  }
  section_end = Clock::now();
  const double skinning_ms = elapsedMs(section_start, section_end);

  section_start = section_end;
  if (audio_system_) {
    audio_system_->update(world_, frame_dt);
  }
  section_end = Clock::now();
  const double audio_ms = elapsedMs(section_start, section_end);

  double framebuffer_ms = 0.0;
  double ui_frame_ms = 0.0;
  double begin_frame_ms = 0.0;
  double particles_ms = 0.0;
  double runtime_modules_ms = 0.0;
  double render_system_ms = 0.0;
  double render_layer_ms = 0.0;
  double render_ui_ms = 0.0;
  double end_frame_ms = 0.0;
  double swap_buffers_ms = 0.0;
  if (graphics_ && render_system_) {
    int fb_width = 0;
    int fb_height = 0;
    section_start = section_end;
    if (window_) {
      window_->getFramebufferSize(fb_width, fb_height);
    }
    section_end = Clock::now();
    framebuffer_ms = elapsedMs(section_start, section_end);

    auto prepare_ui_context = [&](UIContext& ctx) {
      ctx.frame_.dt = frame_dt;
      ctx.frame_.viewport_w = fb_width;
      ctx.frame_.viewport_h = fb_height;
      ctx.frame_.dpi_scale = window_ ? window_->getContentScale() : 1.0f;
      ctx.draw_data_.clear();
      ctx.input_ = &input_;
      ctx.device_ = graphics_.get();
    };

    section_start = section_end;
    if (user_ui_) {
      prepare_ui_context(user_ui_context_);
      user_ui_->onFrame(user_ui_context_);
    }
#if defined(KARMA_DEBUG_UI)
    if (debug_ui_) {
      prepare_ui_context(debug_ui_context_);
      debug_ui_->onFrame(debug_ui_context_);
    }
#endif
    section_end = Clock::now();
    ui_frame_ms = elapsedMs(section_start, section_end);

    renderer::FrameInfo frame{};
    frame.width = fb_width;
    frame.height = fb_height;
    frame.delta_time = frame_dt;

    section_start = section_end;
    graphics_->beginFrame(frame);
    section_end = Clock::now();
    begin_frame_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    if (particle_system_) {
      particle_system_->update(world_, frame_dt, render_alpha);
    }
    section_end = Clock::now();
    particles_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    for (auto& module : runtime_modules_) {
      if (module) {
        module->onUpdate(world_, frame_dt, render_alpha);
      }
    }
    section_end = Clock::now();
    runtime_modules_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    render_system_->update(world_, scene_, frame_dt, render_alpha);
    section_end = Clock::now();
    render_system_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    graphics_->renderLayer(0);
    section_end = Clock::now();
    render_layer_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    if (user_ui_) {
      graphics_->renderUi(user_ui_context_.draw_data_);
    }
#if defined(KARMA_DEBUG_UI)
    if (debug_ui_) {
      graphics_->renderUi(debug_ui_context_.draw_data_);
    }
#endif
    section_end = Clock::now();
    render_ui_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    graphics_->endFrame();
    section_end = Clock::now();
    end_frame_ms = elapsedMs(section_start, section_end);

    section_start = section_end;
    if (window_) {
#if !defined(BZ3_RENDER_BACKEND_DILIGENT)
      window_->swapBuffers();
#endif
    }
    section_end = Clock::now();
    swap_buffers_ms = elapsedMs(section_start, section_end);
  }

  const auto tick_end = Clock::now();
  const double tick_total_ms = elapsedMs(tick_start, tick_end);
  const double raw_frame_ms = static_cast<double>(raw_frame_dt) * 1000.0;
  if (frame_diag_enabled_ &&
      (raw_frame_ms >= static_cast<double>(frame_diag_threshold_ms_) ||
       tick_total_ms >= static_cast<double>(frame_diag_threshold_ms_))) {
    spdlog::info(
        "Engine frame diag: raw_dt={:.3f}ms clamped_dt={:.3f}ms tick={:.3f}ms "
        "events={:.3f} fixed={:.3f}({}) game={:.3f} prefab={:.3f} sync_scene={:.3f} "
        "animation={:.3f} scene_xform={:.3f} skinning={:.3f} audio={:.3f} "
        "fb={:.3f} ui_frame={:.3f} begin={:.3f} particles={:.3f} modules={:.3f} "
        "render_system={:.3f} render_layer={:.3f} render_ui={:.3f} end_frame={:.3f} "
        "swap={:.3f} alpha={:.3f} accumulator={:.3f}",
        raw_frame_ms,
        static_cast<double>(frame_dt) * 1000.0,
        tick_total_ms,
        events_ms,
        fixed_ms,
        fixed_steps,
        game_update_ms,
        prefab_ms,
        sync_scene_ms,
        animation_ms,
        scene_transforms_ms,
        skinning_ms,
        audio_ms,
        framebuffer_ms,
        ui_frame_ms,
        begin_frame_ms,
        particles_ms,
        runtime_modules_ms,
        render_system_ms,
        render_layer_ms,
        render_ui_ms,
        end_frame_ms,
        swap_buffers_ms,
        render_alpha,
        accumulator_);
  }

  if (!running_) {
    if (game_) {
      game_->onShutdown();
    }
    shutdownSubsystems();
    game_ = nullptr;
  }
}

}  // namespace karma::app
