#include "karma/app/engine_app.h"

#include <chrono>
#include <cstdlib>
#include <string>

#include <spdlog/spdlog.h>

#include "karma/debug/debug_overlay.h"

namespace karma::app {
namespace {
uint64_t entityKey(ecs::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32) |
         static_cast<uint64_t>(entity.generation);
}

ecs::Entity entityFromKey(uint64_t key) {
  ecs::Entity entity{};
  entity.index = static_cast<uint32_t>(key >> 32);
  entity.generation = static_cast<uint32_t>(key & 0xFFFFFFFFu);
  return entity;
}
}  // namespace

EngineApp::EngineApp() = default;

EngineApp::~EngineApp() {
  if (game_ && running_) {
    game_->onShutdown();
  }
  shutdownSubsystems();
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

  if (window_) {
    graphics_ = std::make_unique<renderer::GraphicsDevice>(*window_);
    render_system_ = std::make_unique<renderer::RenderSystem>(*graphics_);
  }

  systems_.addSystem(std::make_unique<physics::PhysicsSystem>(physics_));
  audio_system_ = std::make_unique<audio::AudioSystem>(audio_);
  // Register other systems here (PhysicsSystem, AudioSystem, etc.).
}

void EngineApp::shutdownSubsystems() {
  if (ui_) {
    ui_->onShutdown();
    ui_.reset();
  }
  ui_context_ = {};
  render_system_.reset();
  graphics_.reset();
  window_.reset();
  running_ = false;
}

void EngineApp::setUi(std::unique_ptr<UiLayer> ui) {
  if (ui_) {
    ui_->onShutdown();
  }
#if defined(KARMA_DEBUG_UI)
  if (debug_ui_enabled_) {
    ui_ = std::make_unique<debug::DebugOverlayLayer>(&world_,
                                                     &scene_,
                                                     &systems_,
                                                     graphics_.get(),
                                                     config_.shadow_map_size,
                                                     config_.shadow_bias,
                                                     config_.shadow_pcf_radius,
                                                     config_.shadow_raster_depth_bias,
                                                     config_.shadow_raster_slope_bias,
                                                     config_.shadow_receiver_bias_scale,
                                                     config_.shadow_normal_bias_scale);
    return;
  }
#endif
  ui_ = std::move(ui);
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
  fixed_dt_ = config_.fixed_dt;
  const char* debug_env = std::getenv("KARMA_ENGINE_EDITOR_DEBUG");
  debug_ui_enabled_ = debug_env && std::string(debug_env) != "0";
  initSubsystems();
#if defined(KARMA_DEBUG_UI)
  if (debug_ui_enabled_) {
    ui_ = std::make_unique<debug::DebugOverlayLayer>(&world_,
                                                     &scene_,
                                                     &systems_,
                                                     graphics_.get(),
                                                     config_.shadow_map_size,
                                                     config_.shadow_bias,
                                                     config_.shadow_pcf_radius,
                                                     config_.shadow_raster_depth_bias,
                                                     config_.shadow_raster_slope_bias,
                                                     config_.shadow_receiver_bias_scale,
                                                     config_.shadow_normal_bias_scale);
  }
#endif
  if (graphics_) {
    graphics_->setGenerateMips(config_.generate_mipmaps);
    graphics_->setEnvironmentMap(config_.environment_map,
                                 config_.environment_intensity,
                                 config_.environment_draw_skybox);
    graphics_->setAnisotropy(config_.enable_anisotropy, config_.anisotropy_level);
    graphics_->setShadowSettings(config_.shadow_bias,
                                 config_.shadow_map_size,
                                 config_.shadow_pcf_radius,
                                 config_.shadow_raster_depth_bias,
                                 config_.shadow_raster_slope_bias,
                                 config_.shadow_receiver_bias_scale,
                                 config_.shadow_normal_bias_scale);
  }
  game_ = &game;
  running_ = true;
  accumulator_ = 0.0f;
  last_time_ = std::chrono::steady_clock::now();
  game_->bindContext(world_, scene_, input_, physics_, graphics_.get());
  game_->onStart();
}

void EngineApp::requestStop() {
  running_ = false;
}

void EngineApp::syncSceneEntities() {
  for (const ecs::Entity entity : world_.entities()) {
    const uint64_t key = entityKey(entity);
    if (entity_nodes_.find(key) == entity_nodes_.end()) {
      const scene::NodeId node = scene_.createNode(entity);
      entity_nodes_[key] = node;
    }
  }
  for (auto it = entity_nodes_.begin(); it != entity_nodes_.end();) {
    const ecs::Entity entity = entityFromKey(it->first);
    if (!world_.isAlive(entity)) {
      scene_.destroyNode(it->second);
      it = entity_nodes_.erase(it);
    } else {
      ++it;
    }
  }
}

void EngineApp::tick() {
  if (!running_ || !game_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  float frame_dt = std::chrono::duration<float>(now - last_time_).count();
  if (frame_dt > config_.max_frame_dt) {
    frame_dt = config_.max_frame_dt;
  }
  last_time_ = now;
  accumulator_ += frame_dt;

  if (window_) {
    window_->pollEvents();
    if (ui_) {
      for (const auto& event : window_->events()) {
        ui_->onEvent(event);
      }
    }
    input_.update(window_->events());
    window_->clearEvents();
    if (window_->shouldClose()) {
      requestStop();
    }
  }

  if (!running_) {
    if (game_) {
      game_->onShutdown();
    }
    shutdownSubsystems();
    game_ = nullptr;
    return;
  }

  while (accumulator_ >= fixed_dt_) {
    game_->onFixedUpdate(fixed_dt_);
    // Physics runs via SystemGraph.
    systems_.update(world_, fixed_dt_);
    accumulator_ -= fixed_dt_;
  }

  game_->onUpdate(frame_dt);
  if (audio_system_) {
    audio_system_->update(world_, frame_dt);
  }
  if (graphics_ && render_system_) {
    syncSceneEntities();
    int fb_width = 0;
    int fb_height = 0;
    if (window_) {
      window_->getFramebufferSize(fb_width, fb_height);
    }
    if (ui_) {
      ui_context_.frame_.dt = frame_dt;
      ui_context_.frame_.viewport_w = fb_width;
      ui_context_.frame_.viewport_h = fb_height;
      ui_context_.frame_.dpi_scale = window_ ? window_->getContentScale() : 1.0f;
      ui_context_.draw_data_.clear();
      ui_context_.input_ = &input_;
      ui_context_.device_ = graphics_.get();
      ui_->onFrame(ui_context_);
    }
    renderer::FrameInfo frame{};
    frame.width = fb_width;
    frame.height = fb_height;
    frame.delta_time = frame_dt;
    graphics_->beginFrame(frame);
    render_system_->update(world_, scene_, frame_dt);
    graphics_->renderLayer(0);
    if (ui_) {
      graphics_->renderUi(ui_context_.draw_data_);
    }
    graphics_->endFrame();
    if (window_) {
#if !defined(BZ3_RENDER_BACKEND_DILIGENT)
      window_->swapBuffers();
#endif
    }
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
