#include "karma/app.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <thread>
#include <string>
#include <string_view>
#include <system_error>

#include <spdlog/spdlog.h>

#include "karma/assets.h"
#include "karma/prefabs.h"
#include "karma/math.h"
#include "karma/math.h"
#include "karma/core.h"
#include "karma/app.h"
#include "karma/physics.h"
#if defined(KARMA_ENABLE_NAVIGATION)
#include "karma/navigation.h"
#endif
#include "karma/visual.h"
#include "karma/visual.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"
#include "karma/world.h"

#include "../../../third_party/stb_image.h"

namespace karma::app {
namespace {

constexpr std::string_view kStartupEnvironmentMapAssetKey =
    "__engine/startup/environment_map";

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

uint32_t envUint(const char* value, uint32_t fallback) {
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (end == value) {
    return fallback;
  }
  return static_cast<uint32_t>(
      std::min<unsigned long>(parsed, std::numeric_limits<uint32_t>::max()));
}

std::filesystem::path resolveStartupPath(const std::filesystem::path& path) {
  if (path.empty()) {
    return {};
  }

  std::error_code ec;
  if (path.is_absolute()) {
    return path;
  }

  if (std::filesystem::exists(path, ec)) {
    return path;
  }

  std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (ec) {
    return path;
  }
  for (int depth = 0; depth < 8; ++depth) {
    const std::filesystem::path candidate = cwd / path;
    ec.clear();
    if (std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
    const std::filesystem::path parent = cwd.parent_path();
    if (parent.empty() || parent == cwd) {
      break;
    }
    cwd = parent;
  }

  return path;
}

std::optional<physics::MeshColliderGeometry> loadMeshColliderGeometry(
    const assets::AssetRegistry& assets,
    std::string_view mesh_asset_key) {
  if (mesh_asset_key.empty()) {
    return std::nullopt;
  }

  const world::MeshData* mesh = assets.findMeshAsset(mesh_asset_key);
  if (mesh == nullptr) {
    return std::nullopt;
  }

  physics::MeshColliderGeometry geometry;
  geometry.vertices.reserve(mesh->vertices.size());
  for (const glm::vec3& vertex : mesh->vertices) {
    geometry.vertices.push_back(math::fromGlm(vertex));
  }

  geometry.indices.reserve(mesh->indices.size());
  for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
    const uint32_t a = mesh->indices[i];
    const uint32_t b = mesh->indices[i + 1];
    const uint32_t c = mesh->indices[i + 2];
    if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || c >= mesh->vertices.size()) {
      continue;
    }
    geometry.indices.push_back(a);
    geometry.indices.push_back(b);
    geometry.indices.push_back(c);
  }

  if (geometry.vertices.empty() || geometry.indices.empty()) {
    return std::nullopt;
  }
  return geometry;
}

uint32_t packUiColor(math::Color color) {
  auto pack_channel = [](float value) {
    return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  const uint32_t r = pack_channel(color.r);
  const uint32_t g = pack_channel(color.g);
  const uint32_t b = pack_channel(color.b);
  const uint32_t a = pack_channel(color.a);
  return r | (g << 8u) | (b << 16u) | (a << 24u);
}

math::Color scaledColor(math::Color color, float scale, float alpha) {
  color.r *= scale;
  color.g *= scale;
  color.b *= scale;
  color.a *= alpha;
  return color;
}

void addUiQuad(rendering::UIDrawData& out,
               float x,
               float y,
               float w,
               float h,
               const math::Color& color) {
  if (w <= 0.0f || h <= 0.0f) {
    return;
  }
  const uint32_t rgba = packUiColor(color);
  const uint32_t base = static_cast<uint32_t>(out.vertices.size());
  out.vertices.push_back(rendering::UIVertex{.x = x, .y = y, .rgba = rgba});
  out.vertices.push_back(rendering::UIVertex{.x = x + w, .y = y, .rgba = rgba});
  out.vertices.push_back(rendering::UIVertex{.x = x + w, .y = y + h, .rgba = rgba});
  out.vertices.push_back(rendering::UIVertex{.x = x, .y = y + h, .rgba = rgba});
  out.indices.push_back(base);
  out.indices.push_back(base + 1u);
  out.indices.push_back(base + 2u);
  out.indices.push_back(base);
  out.indices.push_back(base + 2u);
  out.indices.push_back(base + 3u);
}

void addUiTexturedQuad(rendering::UIDrawData& out,
                       float x,
                       float y,
                       float w,
                       float h,
                       UITextureHandle texture,
                       const math::Color& color) {
  if (w <= 0.0f || h <= 0.0f || texture == 0) {
    return;
  }
  const uint32_t rgba = packUiColor(color);
  const uint32_t base = static_cast<uint32_t>(out.vertices.size());
  out.vertices.push_back(rendering::UIVertex{.x = x, .y = y, .u = 0.0f, .v = 0.0f, .rgba = rgba});
  out.vertices.push_back(rendering::UIVertex{.x = x + w, .y = y, .u = 1.0f, .v = 0.0f, .rgba = rgba});
  out.vertices.push_back(rendering::UIVertex{.x = x + w, .y = y + h, .u = 1.0f, .v = 1.0f, .rgba = rgba});
  out.vertices.push_back(rendering::UIVertex{.x = x, .y = y + h, .u = 0.0f, .v = 1.0f, .rgba = rgba});
  out.indices.push_back(base);
  out.indices.push_back(base + 1u);
  out.indices.push_back(base + 2u);
  out.indices.push_back(base);
  out.indices.push_back(base + 2u);
  out.indices.push_back(base + 3u);
}

}  // namespace

EngineApp::EngineApp()
    : light_pulse_system_(std::make_unique<visual::LightPulseSystem>()) {}

#if defined(KARMA_DEBUG_UI)
std::unique_ptr<UiLayer> EngineApp::createDebugOverlayUi() {
  if (!debug_ui_enabled_) {
    return nullptr;
  }
  return std::make_unique<app::DebugOverlayLayer>(&world_,
                                                    &scene_,
                                                    &systems_,
                                                    graphics_.get(),
                                                    &assets_,
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
      .assets = &assets_,
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
  const bool startup_diag = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  const auto init_start = core::SteadyClock::now();
  auto stage_start = init_start;
  auto log_init_stage = [&](const char* name, core::SteadyClock::time_point end) {
    if (startup_diag) {
      spdlog::info("Engine startup diag: area=runtime_init stage={} ms={:.2f} total_ms={:.2f}",
                   name,
                   core::elapsedMilliseconds(stage_start, end),
                   core::elapsedMilliseconds(init_start, end));
    }
    stage_start = end;
  };

  window_ = platform::CreateWindow(config_.window);
  log_init_stage("window create", core::SteadyClock::now());

  if (window_) {
    window_->setVsync(config_.vsync);
    window_->setFullscreen(config_.fullscreen);
    window_->setCursorVisible(config_.cursor_visible);
    if (!config_.window.icon_path.empty()) {
      window_->setIcon(config_.window.icon_path);
    }
  }
  log_init_stage("window configure", core::SteadyClock::now());

  input_.setWindow(window_.get());
  log_init_stage("input bind window", core::SteadyClock::now());

  if (window_) {
    rendering::GraphicsDeviceCreateInfo graphics_create_info{};
    graphics_create_info.vsync = config_.vsync;
    graphics_create_info.present_mode = config_.present_mode;
    graphics_create_info.execution_mode = config_.renderer_execution_mode;
    graphics_ = std::make_unique<rendering::GraphicsDevice>(*window_, graphics_create_info);
    log_init_stage("graphics device create", core::SteadyClock::now());

    render_system_ = std::make_unique<rendering::RenderSystem>(*graphics_, assets_);
    log_init_stage("render system create", core::SteadyClock::now());

    particle_system_ = std::make_unique<visual::particles::ParticleSystem>(graphics_.get(), &assets_);
    log_init_stage("particle system create", core::SteadyClock::now());
  }

  auto physics_system = std::make_unique<physics::PhysicsSystem>(physics_);
  physics_system->setMeshColliderGeometryProvider(
      [this](std::string_view mesh_asset_key) {
        return loadMeshColliderGeometry(assets_, mesh_asset_key);
      });
  const auto physics_system_id = systems_.addSystem(std::move(physics_system));
  log_init_stage("physics system create", core::SteadyClock::now());

  const auto collision_system_id =
      systems_.addSystem(std::make_unique<physics::CollisionEventSystem>());
  systems_.addDependency(collision_system_id, physics_system_id);
  log_init_stage("collision system create", core::SteadyClock::now());
#if defined(KARMA_ENABLE_NAVIGATION)
  systems_.addSystem(std::make_unique<navigation::NavigationSystem>(&assets_));
  log_init_stage("navigation system create", core::SteadyClock::now());
#endif
  audio_system_ = std::make_unique<audio::AudioSystem>(audio_, &assets_);
  log_init_stage("audio system create", core::SteadyClock::now());
  // Register other systems here (PhysicsSystem, AudioSystem, etc.).
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=runtime_init stage=total ms={:.2f}",
                 core::elapsedMillisecondsSince(init_start));
  }
}

void EngineApp::warmUpRenderer() {
  if (!graphics_ || !render_system_) {
    return;
  }

  const bool startup_diag = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  const auto warmup_start = core::SteadyClock::now();
  auto section_start = warmup_start;
  syncSceneEntities();
  auto section_end = core::SteadyClock::now();
  auto log_stage = [&](const char* name,
                       const core::SteadyClock::time_point start,
                       const core::SteadyClock::time_point end) {
    if (startup_diag) {
      spdlog::info("Renderer warm-up stage '{}' start_ms={:.2f} ms={:.2f} total_ms={:.2f}",
                   name,
                   core::elapsedMilliseconds(warmup_start, start),
                   core::elapsedMilliseconds(start, end),
                   core::elapsedMilliseconds(warmup_start, end));
    }
  };
  log_stage("sync scene", section_start, section_end);

  section_start = section_end;
  int fb_width = 0;
  int fb_height = 0;
  if (window_) {
    window_->getFramebufferSize(fb_width, fb_height);
  }
  section_end = core::SteadyClock::now();
  log_stage("framebuffer query", section_start, section_end);
  if (fb_width <= 0 || fb_height <= 0) {
    spdlog::info("Renderer warm-up skipped: framebuffer={}x{}", fb_width, fb_height);
    return;
  }

  rendering::FrameInfo frame{};
  frame.width = fb_width;
  frame.height = fb_height;
  frame.delta_time = 0.0f;

  const bool include_ui_prewarm =
      user_ui_ != nullptr
#if defined(KARMA_DEBUG_UI)
      || debug_ui_ != nullptr
#endif
      ;
  auto log_pass_stage = [&](std::string_view pass,
                            const char* name,
                            const core::SteadyClock::time_point start,
                            const core::SteadyClock::time_point end) {
    if (startup_diag) {
      spdlog::info("Renderer warm-up stage '{} {}' start_ms={:.2f} ms={:.2f} total_ms={:.2f}",
                   pass,
                   name,
                   core::elapsedMilliseconds(warmup_start, start),
                   core::elapsedMilliseconds(start, end),
                   core::elapsedMilliseconds(warmup_start, end));
    }
  };
  auto run_warmup_frame = [&](std::string_view pass, bool run_module_warmup) {
    section_start = section_end;
    graphics_->beginFrame(frame);
    section_end = core::SteadyClock::now();
    log_pass_stage(pass, "begin frame", section_start, section_end);

    section_start = section_end;
    graphics_->prewarmRendererResources(include_ui_prewarm);
    section_end = core::SteadyClock::now();
    log_pass_stage(pass, "renderer resource prewarm", section_start, section_end);

    section_start = section_end;
    animation_system_.update(world_, scene_, 0.0f);
    section_end = core::SteadyClock::now();
    log_pass_stage(pass, "animation", section_start, section_end);

    section_start = section_end;
    world::updateWorldTransforms(world_, scene_);
    section_end = core::SteadyClock::now();
    log_pass_stage(pass, "scene transforms", section_start, section_end);

    section_start = section_end;
    deformation_system_.update(world_, scene_, *graphics_, &assets_);
    section_end = core::SteadyClock::now();
    log_pass_stage(pass, "mesh deformation", section_start, section_end);

    if (particle_system_) {
      section_start = section_end;
      light_pulse_system_->update(world_, 0.0f);
      section_end = core::SteadyClock::now();
      log_pass_stage(pass, "light pulse", section_start, section_end);

      section_start = section_end;
      particle_system_->update(world_, 0.0f, 1.0f);
      section_end = core::SteadyClock::now();
      log_pass_stage(pass, "particles", section_start, section_end);
    }

    if (run_module_warmup) {
      section_start = section_end;
      for (auto& module : runtime_modules_) {
        if (module) {
          module->onWarmUp(world_);
        }
      }
      section_end = core::SteadyClock::now();
      log_pass_stage(pass, "runtime modules", section_start, section_end);
    }

    section_start = section_end;
    render_system_->update(world_, scene_, 0.0f, 1.0f);
    section_end = core::SteadyClock::now();
    log_pass_stage(pass, "render system update", section_start, section_end);

    section_start = section_end;
    graphics_->endFrame(true);
    section_end = core::SteadyClock::now();
    log_pass_stage(pass, "end frame", section_start, section_end);
  };

  struct CameraWarmupRestore {
    world::Entity entity{};
    math::Quat local_rotation{};
  };

  auto run_camera_sweep = [&](uint32_t steps) {
    if (steps == 0u) {
      return;
    }

    std::vector<CameraWarmupRestore> cameras;
    world_.forEach<components::CameraComponent, components::TransformComponent>(
        [&](const world::Entity entity) {
          const auto& camera = world_.get<components::CameraComponent>(entity);
          if (!camera.is_primary) {
            return true;
          }
          const auto& transform = world_.get<components::TransformComponent>(entity);
          cameras.push_back(CameraWarmupRestore{
              .entity = entity,
              .local_rotation = transform.localRotation(),
          });
          return true;
        });
    if (cameras.empty()) {
      spdlog::info("Renderer camera sweep warm-up skipped: no primary camera");
      return;
    }

    constexpr float kPi = 3.14159265358979323846f;
    for (uint32_t step = 0; step < steps; ++step) {
      const float yaw =
          (static_cast<float>(step + 1u) / static_cast<float>(steps + 1u)) * 2.0f * kPi;
      const math::Quat yaw_rotation = math::fromYawPitch(yaw, 0.0f);
      for (const CameraWarmupRestore& camera : cameras) {
        if (!world_.isAlive(camera.entity) ||
            !world_.has<components::TransformComponent>(camera.entity)) {
          continue;
        }
        auto& transform = world_.get<components::TransformComponent>(camera.entity);
        transform.setRotation(math::mul(yaw_rotation, camera.local_rotation));
      }
      const std::string pass_name =
          "camera_sweep" + std::to_string(static_cast<unsigned long long>(step + 1u));
      run_warmup_frame(pass_name, false);
    }

    for (const CameraWarmupRestore& camera : cameras) {
      if (!world_.isAlive(camera.entity) ||
          !world_.has<components::TransformComponent>(camera.entity)) {
        continue;
      }
      auto& transform = world_.get<components::TransformComponent>(camera.entity);
      transform.setRotation(camera.local_rotation);
    }
    world::updateWorldTransforms(world_, scene_);
  };

  run_warmup_frame("pass1", true);
  run_warmup_frame("pass2", false);
  run_camera_sweep(config_.renderer_warmup_camera_sweep_steps);
  run_warmup_frame("validation", false);
  const rendering::RendererFrameTimingStats warmup_validation_timing =
      graphics_->getRendererFrameTimingStats();
  const rendering::InstancingStats warmup_validation_instancing =
      graphics_->getInstancingStats();
  constexpr uint64_t kWarmupValidationInstanceUploadWarningBytes = 1024u;
  if (warmup_validation_timing.resource_creation_count > 0u ||
      warmup_validation_timing.pipeline_creation_count > 0u ||
      warmup_validation_instancing.instance_upload_bytes >
          kWarmupValidationInstanceUploadWarningBytes) {
    spdlog::warn(
        "Renderer warm-up validation created resources: resources={} ({:.2f} ms) "
        "pipelines={} ({:.2f} ms) instance_uploads={} bytes={} upload_ms={:.2f}",
        warmup_validation_timing.resource_creation_count,
        warmup_validation_timing.resource_creation_ms,
        warmup_validation_timing.pipeline_creation_count,
        warmup_validation_timing.pipeline_creation_ms,
        warmup_validation_instancing.instance_buffer_updates,
        warmup_validation_instancing.instance_upload_bytes,
        warmup_validation_instancing.instance_upload_ms);
  }

  section_start = section_end;
  graphics_->flushRenderStateCache();
  section_end = core::SteadyClock::now();
  log_stage("render state cache flush", section_start, section_end);

  spdlog::info("Renderer warm-up took {:.2f} ms",
               core::elapsedMillisecondsSince(warmup_start));
}

void EngineApp::shutdownSubsystems() {
  if (user_ui_) {
    user_ui_->onShutdown();
    user_ui_.reset();
  }
  user_ui_context_.reset();
#if defined(KARMA_DEBUG_UI)
  if (debug_ui_) {
    debug_ui_->onShutdown();
    debug_ui_.reset();
  }
  debug_ui_context_.reset();
#endif
  if (render_system_ && startup_prewarm_handle_.valid()) {
    render_system_->releasePrewarm(startup_prewarm_handle_);
    startup_prewarm_handle_ = {};
  }
  for (auto it = startup_asset_package_handles_.rbegin();
       it != startup_asset_package_handles_.rend();
       ++it) {
    assets::unloadAssetPackage(assets_, *it);
  }
  startup_asset_package_handles_.clear();
  render_system_.reset();
  prefabs::clearPrefabAssetPackages();
  particle_system_.reset();
  for (auto& module : runtime_modules_) {
    if (module) {
      module->onDetach();
    }
  }
  releaseLoadingSplashTexture();
  graphics_.reset();
  window_.reset();
  running_ = false;
}

void EngineApp::shutdownRunningGame() {
  if (game_) {
    game_->onShutdown();
  }
  shutdownSubsystems();
  game_ = nullptr;
}

void EngineApp::setUi(std::unique_ptr<UiLayer> ui) {
  if (user_ui_) {
    user_ui_->onShutdown();
    user_ui_context_.reset();
  }
  user_ui_ = std::move(ui);
}

void EngineApp::setCursorVisible(bool visible) {
  if (window_) {
    window_->setCursorVisible(visible);
  }
  config_.cursor_visible = visible;
}

bool EngineApp::ensureLoadingSplashTexture() {
  if (loading_splash_texture_ != rendering::kInvalidTexture) {
    return true;
  }
  if (!graphics_ || config_.loading_splash.image_path.empty()) {
    return false;
  }

  const bool startup_diag = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  int width = 0;
  int height = 0;
  int comp = 0;
  unsigned char* pixels =
      stbi_load(config_.loading_splash.image_path.string().c_str(), &width, &height, &comp, 4);
  auto stage_end = core::SteadyClock::now();
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=image decode ms={:.2f}",
                 core::elapsedMilliseconds(stage_start, stage_end));
  }
  if (!pixels || width <= 0 || height <= 0) {
    if (pixels) {
      stbi_image_free(pixels);
    }
    spdlog::warn("Loading splash image '{}' could not be loaded",
                 config_.loading_splash.image_path.string());
    return false;
  }

  stage_start = stage_end;
  const rendering::TextureId texture = graphics_->createTextureRGBA8(width, height, pixels);
  stage_end = core::SteadyClock::now();
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=texture upload ms={:.2f}",
                 core::elapsedMilliseconds(stage_start, stage_end));
  }
  stbi_image_free(pixels);
  if (texture == rendering::kInvalidTexture) {
    spdlog::warn("Loading splash image '{}' could not be uploaded",
                 config_.loading_splash.image_path.string());
    return false;
  }

  loading_splash_texture_ = texture;
  loading_splash_texture_width_ = width;
  loading_splash_texture_height_ = height;
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=texture total ms={:.2f}",
                 core::elapsedMillisecondsSince(total_start));
  }
  return true;
}

void EngineApp::releaseLoadingSplashTexture() {
  std::lock_guard<std::mutex> lock(loading_splash_graphics_mutex_);
  if (graphics_ && loading_splash_texture_ != rendering::kInvalidTexture) {
    graphics_->destroyTexture(loading_splash_texture_);
  }
  loading_splash_texture_ = rendering::kInvalidTexture;
  loading_splash_texture_width_ = 0;
  loading_splash_texture_height_ = 0;
  loading_splash_presented_ = false;
}

bool EngineApp::renderLoadingSplash(float progress) {
  if (!config_.loading_splash.enabled || !window_ || !graphics_) {
    return true;
  }

  const bool startup_diag_requested = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  static int startup_splash_diag_frames = 0;
  const bool startup_diag = startup_diag_requested && startup_splash_diag_frames < 8;
  if (startup_diag_requested) {
    ++startup_splash_diag_frames;
  }
  const auto frame_start = core::SteadyClock::now();
  auto stage_start = frame_start;
  window_->pollEvents();
  const bool close_requested = window_->shouldClose();
  window_->clearEvents();
  auto stage_end = core::SteadyClock::now();
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=poll events ms={:.2f}",
                 core::elapsedMilliseconds(stage_start, stage_end));
  }
  if (close_requested) {
    return false;
  }

  stage_start = stage_end;
  int fb_width = 0;
  int fb_height = 0;
  window_->getFramebufferSize(fb_width, fb_height);
  stage_end = core::SteadyClock::now();
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=framebuffer query ms={:.2f}",
                 core::elapsedMilliseconds(stage_start, stage_end));
  }
  if (fb_width <= 0 || fb_height <= 0) {
    return true;
  }

  std::lock_guard<std::mutex> lock(loading_splash_graphics_mutex_);
  const auto& splash = config_.loading_splash;
  const float width = static_cast<float>(fb_width);
  const float height = static_cast<float>(fb_height);
  const float clamped_progress = std::clamp(progress, 0.0f, 1.0f);
  stage_start = stage_end;
  const bool has_splash_image = ensureLoadingSplashTexture();
  stage_end = core::SteadyClock::now();
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=ensure texture ms={:.2f}",
                 core::elapsedMilliseconds(stage_start, stage_end));
  }

  stage_start = stage_end;
  const float unit = std::clamp(height / 160.0f, 3.0f, 7.0f);
  const float center_x = width * 0.5f;
  const float image_max_w = width * 0.48f;
  const float image_max_h = height * 0.56f;
  const float image_aspect =
      loading_splash_texture_height_ > 0
          ? static_cast<float>(loading_splash_texture_width_) /
                static_cast<float>(loading_splash_texture_height_)
          : 1.0f;
  float image_w = image_max_w;
  float image_h = image_w / std::max(image_aspect, 0.01f);
  if (image_h > image_max_h) {
    image_h = image_max_h;
    image_w = image_h * image_aspect;
  }
  const float image_x = center_x - image_w * 0.5f;
  const float image_y = height * 0.45f - image_h * 0.5f;
  const float bar_w = std::min(std::max(width * 0.26f, unit * 34.0f), image_w);
  const float bar_h = std::max(3.0f, unit * 0.65f);
  const float bar_x = center_x - bar_w * 0.5f;
  const float bar_y = std::min(height - unit * 10.0f, image_y + image_h + unit * 5.5f);

  rendering::UIDrawData draw_data;
  uint32_t command_index_offset = 0;
  auto append_command = [&](UITextureHandle texture) {
    const uint32_t index_count =
        static_cast<uint32_t>(draw_data.indices.size()) - command_index_offset;
    if (index_count == 0u) {
      return;
    }
    rendering::UIDrawCmd cmd{};
    cmd.index_offset = command_index_offset;
    cmd.index_count = index_count;
    cmd.texture = texture;
    draw_data.commands.push_back(cmd);
    command_index_offset += index_count;
  };

  addUiQuad(draw_data, 0.0f, 0.0f, width, height, splash.background);
  append_command(0);

  const math::Color accent_soft = scaledColor(splash.accent, 0.18f, 1.0f);
  if (has_splash_image) {
    addUiTexturedQuad(draw_data,
                      image_x,
                      image_y,
                      image_w,
                      image_h,
                      static_cast<UITextureHandle>(loading_splash_texture_),
                      splash.foreground);
    append_command(static_cast<UITextureHandle>(loading_splash_texture_));
  }

  addUiQuad(draw_data, bar_x, bar_y, bar_w, bar_h, accent_soft);
  addUiQuad(draw_data,
            bar_x,
            bar_y,
            std::max(bar_h, bar_w * clamped_progress),
            bar_h,
            splash.accent);
  append_command(0);
  stage_end = core::SteadyClock::now();
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=build ui ms={:.2f}",
                 core::elapsedMilliseconds(stage_start, stage_end));
  }

  rendering::FrameInfo frame{};
  frame.width = fb_width;
  frame.height = fb_height;
  frame.delta_time = 0.0f;
  stage_start = stage_end;
  graphics_->beginFrame(frame);
  graphics_->renderUi(draw_data);
  graphics_->endFrame(true);
  stage_end = core::SteadyClock::now();
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=graphics frame ms={:.2f}",
                 core::elapsedMilliseconds(stage_start, stage_end));
  }
  loading_splash_presented_ = true;
#if !defined(KARMA_RENDER_BACKEND_DILIGENT)
  window_->swapBuffers();
#endif
  if (startup_diag) {
    spdlog::info("Engine startup diag: area=loading_splash stage=frame total ms={:.2f}",
                 core::elapsedMillisecondsSince(frame_start));
  }
  return true;
}

bool EngineApp::shouldRenderLoadingSplash(
    core::SteadyClock::time_point startup_start) const {
  if (!config_.loading_splash.enabled) {
    return false;
  }
  if (loading_splash_presented_) {
    return true;
  }
  const int show_after_ms = std::max(0, config_.loading_splash.show_after_ms);
  return core::elapsedMillisecondsSince(startup_start) >=
         static_cast<double>(show_after_ms);
}

bool EngineApp::renderLoadingSplashIfDue(
    float progress, core::SteadyClock::time_point startup_start) {
  if (!shouldRenderLoadingSplash(startup_start)) {
    return true;
  }
  return renderLoadingSplash(progress);
}

bool EngineApp::presentInitialLoadingSplash(
    float progress, core::SteadyClock::time_point startup_start) {
  if (!config_.loading_splash.enabled || !window_ || !graphics_) {
    return true;
  }
  if (!shouldRenderLoadingSplash(startup_start)) {
    return true;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
  while (!loading_splash_presented_) {
    if (!renderLoadingSplash(progress)) {
      return false;
    }
    if (loading_splash_presented_ || std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }
  return true;
}

void EngineApp::start(GameInterface& game, const EngineConfig& config) {
  if (running_) {
    return;
  }
  spdlog::set_level(spdlog::level::trace);
  config_ = config;
  const bool has_custom_default_graph =
      !config_.default_frame_graph.resources.empty() ||
      !config_.default_frame_graph.passes.empty() ||
      !config_.default_frame_graph.frame_graph_key.empty();
  assets_.registerFrameGraph(
      std::string(rendering::kDefaultFrameGraphKey),
      has_custom_default_graph ? config_.default_frame_graph
                               : rendering::defaultFrameGraphDesc());
  loading_splash_presented_ = false;
  config_.loading_splash.image_path = resolveStartupPath(config_.loading_splash.image_path);
  config_.environment_map_source_path =
      resolveStartupPath(config_.environment_map_source_path);
  if (!config_.environment_map_source_path.empty()) {
    assets_.registerEnvironmentMap(
        std::string(kStartupEnvironmentMapAssetKey),
        assets::EnvironmentMapAsset{.path = config_.environment_map_source_path});
  }
  if (const char* vsync_env = std::getenv("KARMA_ENGINE_VSYNC")) {
    config_.vsync = envFlagEnabled(vsync_env);
    config_.present_mode = rendering::PresentMode::Auto;
    spdlog::info("KARMA_ENGINE_VSYNC override: {}", config_.vsync ? "on" : "off");
  }
  if (const char* frame_pacing_env = std::getenv("KARMA_ENGINE_FRAME_PACING_FPS")) {
    config_.frame_pacing_fps =
        std::max(0.0f, envFloat(frame_pacing_env, config_.frame_pacing_fps));
    spdlog::info("KARMA_ENGINE_FRAME_PACING_FPS override: {:.2f}",
                 config_.frame_pacing_fps);
  }
  if (const char* frame_pacing_env = std::getenv("KARMA_ENGINE_FRAME_PACE_FPS")) {
    config_.frame_pacing_fps =
        std::max(0.0f, envFloat(frame_pacing_env, config_.frame_pacing_fps));
    spdlog::info("KARMA_ENGINE_FRAME_PACE_FPS override: {:.2f}",
                 config_.frame_pacing_fps);
  }
  if (const char* skip_present_env =
          std::getenv("KARMA_ENGINE_SKIP_PRESENT_ON_MOUSE_BUTTON")) {
    config_.skip_present_on_mouse_button = envFlagEnabled(skip_present_env);
    spdlog::info("KARMA_ENGINE_SKIP_PRESENT_ON_MOUSE_BUTTON override: {}",
                 config_.skip_present_on_mouse_button ? "on" : "off");
  }
  if (const char* skip_present_frames_env =
          std::getenv("KARMA_ENGINE_MOUSE_BUTTON_PRESENT_SKIP_FRAMES")) {
    config_.mouse_button_present_skip_frames =
        envUint(skip_present_frames_env, config_.mouse_button_present_skip_frames);
    spdlog::info("KARMA_ENGINE_MOUSE_BUTTON_PRESENT_SKIP_FRAMES override: {}",
                 config_.mouse_button_present_skip_frames);
  }
  if (const char* camera_sweep_env =
          std::getenv("KARMA_RENDER_WARMUP_CAMERA_SWEEP_STEPS")) {
    config_.renderer_warmup_camera_sweep_steps =
        envUint(camera_sweep_env, config_.renderer_warmup_camera_sweep_steps);
    spdlog::info("KARMA_RENDER_WARMUP_CAMERA_SWEEP_STEPS override: {}",
                 config_.renderer_warmup_camera_sweep_steps);
  }
  config_.frame_pacing_fps = std::max(0.0f, config_.frame_pacing_fps);
  if (config_.skip_present_on_mouse_button) {
    spdlog::info("Engine will skip swapchain present for {} frame(s) after mouse-button events",
                 config_.mouse_button_present_skip_frames);
  }
  if (config_.renderer_warmup_camera_sweep_steps > 0u) {
    spdlog::info("Renderer camera sweep warm-up enabled: {} extra views",
                 config_.renderer_warmup_camera_sweep_steps);
  }
  next_frame_pace_time_ = {};
  if (config_.frame_pacing_fps > 0.0f) {
    spdlog::info("Engine frame pacing enabled at {:.2f} FPS", config_.frame_pacing_fps);
  }
  fixed_dt_ = config_.fixed_dt;
  const char* debug_env = std::getenv("KARMA_ENGINE_EDITOR_DEBUG");
  debug_ui_enabled_ = debug_env && std::string(debug_env) != "0";
  const bool startup_diag = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  const auto startup_start = core::SteadyClock::now();
  auto section_start = startup_start;
  auto section_end = startup_start;
  auto log_startup_stage = [&](const char* name,
                               const core::SteadyClock::time_point start,
                               const core::SteadyClock::time_point end) {
    if (startup_diag) {
      spdlog::info("Engine startup timeline: stage='{}' start_ms={:.2f} ms={:.2f} total_ms={:.2f}",
                   name,
                   core::elapsedMilliseconds(startup_start, start),
                   core::elapsedMilliseconds(start, end),
                   core::elapsedMilliseconds(startup_start, end));
    }
  };
  auto finish_startup_stage = [&](const char* name) {
    section_end = core::SteadyClock::now();
    log_startup_stage(name, section_start, section_end);
    section_start = section_end;
  };

  initSubsystems();
  finish_startup_stage("init subsystems");

  if (!presentInitialLoadingSplash(0.05f, startup_start)) {
    shutdownSubsystems();
    return;
  }
  finish_startup_stage("initial loading splash checkpoint");

#if defined(KARMA_DEBUG_UI)
  debug_ui_ = createDebugOverlayUi();
  finish_startup_stage("debug ui");
#endif

  if (graphics_) {
    graphics_->setClearColor(config_.background_color);
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
  finish_startup_stage("graphics settings");

  if (!renderLoadingSplashIfDue(0.18f, startup_start)) {
    shutdownSubsystems();
    return;
  }
  finish_startup_stage("loading splash checkpoint after graphics settings");

  startup_asset_package_handles_.clear();
  for (const std::filesystem::path& package_path : config_.startup_asset_packages) {
    const std::filesystem::path resolved_package_path = resolveStartupPath(package_path);
    std::string diagnostic;
    auto package = assets::importAssetPackage(assets_, resolved_package_path, &diagnostic);
    if (!package.has_value()) {
      spdlog::error("Failed to import startup asset package '{}': {}",
                    resolved_package_path.string(),
                    diagnostic);
      shutdownSubsystems();
      return;
    }
    startup_asset_package_handles_.push_back(std::move(*package));
  }
  finish_startup_stage("startup asset packages");

  game_ = &game;
  running_ = true;
  accumulator_ = 0.0f;
  fixed_tick_ = 0;
  frame_tick_ = 0;
  last_mouse_button_frame_tick_ = std::numeric_limits<uint64_t>::max();
  last_synced_entity_version_ = std::numeric_limits<uint64_t>::max();
  game_->bindContext(world_,
                     scene_,
                     input_,
                     physics_,
                     graphics_.get(),
                     render_system_.get(),
                     assets_,
                     systems_);
  finish_startup_stage("bind game context");

  prefabs::bindPrefabAssetRegistry(&assets_);

  for (auto& module : runtime_modules_) {
    if (module) {
      module->onAttach(makeRuntimeModuleContext());
    }
  }
  finish_startup_stage("runtime module attach");

  auto shutdown_started_game = [&]() {
    shutdownRunningGame();
  };

  bool startup_close_requested = false;
  std::exception_ptr startup_exception;
  core::SteadyClock::time_point game_on_start_body_start = section_start;
  core::SteadyClock::time_point game_on_start_body_end = section_start;
  core::SteadyClock::time_point game_on_start_wait_start = section_start;
  core::SteadyClock::time_point game_on_start_wait_end = section_start;
  int startup_wait_poll_count = 0;
  int startup_wait_sleep_count = 0;
  int startup_wait_splash_frames = 0;
  double startup_wait_poll_ms = 0.0;
  double startup_wait_sleep_ms = 0.0;
  double startup_wait_splash_ms = 0.0;
  if (config_.loading_splash.enabled) {
    std::atomic<bool> startup_done{false};
    std::thread startup_thread([&]() {
      game_on_start_body_start = core::SteadyClock::now();
      try {
        game_->onStart();
      } catch (...) {
        startup_exception = std::current_exception();
      }
      game_on_start_body_end = core::SteadyClock::now();
      startup_done.store(true, std::memory_order_release);
    });

    const int target_fps = std::clamp(config_.loading_splash.target_fps, 1, 240);
    const auto frame_interval = std::chrono::milliseconds(1000 / target_fps);
    const auto async_start_time = core::SteadyClock::now();
    game_on_start_wait_start = async_start_time;
    auto next_frame_time = std::chrono::steady_clock::now();
    while (!startup_done.load(std::memory_order_acquire)) {
      const double elapsed_seconds = core::elapsedSeconds(async_start_time,
                                                          core::SteadyClock::now());
      const float progress =
          std::min(0.58f, 0.18f + static_cast<float>(elapsed_seconds) * 0.10f);
      if (!shouldRenderLoadingSplash(startup_start)) {
        if (!startup_close_requested && window_) {
          const auto poll_start = core::SteadyClock::now();
          window_->pollEvents();
          startup_close_requested = window_->shouldClose();
          window_->clearEvents();
          startup_wait_poll_ms +=
              core::elapsedMilliseconds(poll_start, core::SteadyClock::now());
          ++startup_wait_poll_count;
        }
        const auto sleep_start = core::SteadyClock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        startup_wait_sleep_ms +=
            core::elapsedMilliseconds(sleep_start, core::SteadyClock::now());
        ++startup_wait_sleep_count;
        next_frame_time = std::chrono::steady_clock::now();
        continue;
      }
      const auto now = std::chrono::steady_clock::now();
      if (now < next_frame_time) {
        const auto sleep_start = core::SteadyClock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        startup_wait_sleep_ms +=
            core::elapsedMilliseconds(sleep_start, core::SteadyClock::now());
        ++startup_wait_sleep_count;
        continue;
      }
      const auto splash_start = core::SteadyClock::now();
      if (!startup_close_requested && !renderLoadingSplash(progress)) {
        startup_close_requested = true;
      }
      startup_wait_splash_ms +=
          core::elapsedMilliseconds(splash_start, core::SteadyClock::now());
      ++startup_wait_splash_frames;
      next_frame_time = std::chrono::steady_clock::now() + frame_interval;
    }
    startup_thread.join();
    game_on_start_wait_end = core::SteadyClock::now();
  } else {
    game_on_start_body_start = core::SteadyClock::now();
    game_->onStart();
    game_on_start_body_end = core::SteadyClock::now();
    game_on_start_wait_start = game_on_start_body_start;
    game_on_start_wait_end = game_on_start_body_end;
  }
  finish_startup_stage("game onStart");
  if (startup_diag) {
    spdlog::info(
        "Engine startup diag: area=game_onStart body_ms={:.2f} wait_loop_ms={:.2f} "
        "polls={} poll_ms={:.2f} sleeps={} sleep_ms={:.2f} splash_frames={} splash_ms={:.2f}",
        core::elapsedMilliseconds(game_on_start_body_start, game_on_start_body_end),
        core::elapsedMilliseconds(game_on_start_wait_start, game_on_start_wait_end),
        startup_wait_poll_count,
        startup_wait_poll_ms,
        startup_wait_sleep_count,
        startup_wait_sleep_ms,
        startup_wait_splash_frames,
        startup_wait_splash_ms);
  }

  if (startup_exception) {
    shutdownSubsystems();
    game_ = nullptr;
    std::rethrow_exception(startup_exception);
  }

  if (startup_close_requested) {
    shutdown_started_game();
    return;
  }
  if (!renderLoadingSplashIfDue(0.62f, startup_start)) {
    shutdown_started_game();
    return;
  }
  finish_startup_stage("loading splash checkpoint after game onStart");

  systems_.update(world_, 0.0f);
  finish_startup_stage("initial systems update");
#if defined(KARMA_ENABLE_NAVIGATION)
  if (startup_diag) {
    if (const auto* nav_system = systems_.findSystem<navigation::NavigationSystem>()) {
      const auto& stats = nav_system->stats();
      spdlog::info(
          "Engine startup nav stats: update={:.2f}ms rebuild={:.2f}ms submit={:.2f}ms move={:.2f}ms apply={:.2f}ms",
          stats.last_update_ms,
          stats.last_rebuild_ms,
          stats.last_submit_ms,
          stats.last_move_ms,
          stats.last_apply_ms);
    }
  }
#endif

  if (graphics_) {
    std::filesystem::path startup_environment_path;
    if (const assets::EnvironmentMapAsset* environment =
            assets_.findEnvironmentMap(kStartupEnvironmentMapAssetKey)) {
      startup_environment_path = environment->path;
    }
    graphics_->setEnvironmentMap(startup_environment_path,
                                 config_.environment_intensity,
                                 config_.environment_draw_skybox);
    spdlog::info("Engine environment setup took {:.2f} ms",
                 core::elapsedMillisecondsSince(section_start));
    finish_startup_stage("engine environment setup");
  }
  if (render_system_ && config_.prewarm_startup_packages &&
      !startup_asset_package_handles_.empty()) {
    std::vector<std::string> mesh_keys;
    std::vector<std::string> material_keys;
    std::vector<std::string> texture_keys;
    for (const auto& package : startup_asset_package_handles_) {
      for (const auto& asset : package.assets) {
        if (asset.type == "mesh") {
          mesh_keys.push_back(asset.key);
        } else if (asset.type == "material") {
          material_keys.push_back(asset.key);
        } else if (asset.type == "texture" || asset.type == "texture_rgba8") {
          texture_keys.push_back(asset.key);
        }
      }
    }
    finish_startup_stage("startup asset prewarm collect keys");
    if (!renderLoadingSplashIfDue(0.74f, startup_start)) {
      shutdown_started_game();
      return;
    }
    finish_startup_stage("loading splash checkpoint before asset prewarm");
    startup_prewarm_handle_ =
        render_system_->prewarmAssets(mesh_keys, material_keys, texture_keys);
    finish_startup_stage("startup asset prewarm");
  }
  if (loading_splash_presented_ && !renderLoadingSplash(0.84f)) {
    shutdown_started_game();
    return;
  }
  if (loading_splash_presented_) {
    finish_startup_stage("final loading splash frame");
  }
  warmUpRenderer();
  finish_startup_stage("renderer warm-up");
  spdlog::info("Engine startup through warm-up took {:.2f} ms",
               core::elapsedMilliseconds(startup_start, section_end));
  accumulator_ = 0.0f;
  last_time_ = core::SteadyClock::now();
  next_frame_pace_time_ = {};
}

void EngineApp::requestStop() {
  running_ = false;
}

void EngineApp::syncSceneEntities() {
  const uint64_t entity_version = world_.entityVersion();
  if (entity_version == last_synced_entity_version_) {
    return;
  }

  for (const world::Entity entity : world_.entities()) {
    if (scene_.findNode(entity) == world::Node::kInvalidId) {
      scene_.createNode(entity);
    }
  }

  std::vector<world::NodeId> stale_nodes;
  for (const auto& node : scene_.nodes()) {
    if (!scene_.isAlive(node.id) || !node.entity.isValid()) {
      continue;
    }
    if (!world_.isAlive(node.entity)) {
      stale_nodes.push_back(node.id);
    }
  }
  for (const world::NodeId id : stale_nodes) {
    scene_.destroyNode(id);
  }
  last_synced_entity_version_ = entity_version;
}

double EngineApp::applyFramePacing() {
  if (config_.frame_pacing_fps <= 0.0f) {
    next_frame_pace_time_ = {};
    return 0.0;
  }

  const auto frame_interval =
      std::chrono::duration_cast<core::SteadyClock::duration>(
          std::chrono::duration<double>(1.0 / static_cast<double>(config_.frame_pacing_fps)));
  if (frame_interval <= core::SteadyClock::duration::zero()) {
    next_frame_pace_time_ = {};
    return 0.0;
  }

  const auto before_sleep = core::SteadyClock::now();
  if (next_frame_pace_time_ == core::SteadyClock::time_point{}) {
    next_frame_pace_time_ = before_sleep + frame_interval;
    return 0.0;
  }

  double sleep_ms = 0.0;
  if (before_sleep < next_frame_pace_time_) {
    std::this_thread::sleep_until(next_frame_pace_time_);
    sleep_ms = core::elapsedMilliseconds(before_sleep, core::SteadyClock::now());
  }

  const auto after_sleep = core::SteadyClock::now();
  while (next_frame_pace_time_ <= after_sleep) {
    next_frame_pace_time_ += frame_interval;
  }
  return sleep_ms;
}

void EngineApp::tick() {
  if (!running_ || !game_) {
    return;
  }
  const uint64_t frame_tick = frame_tick_++;

  if (!frame_diag_initialized_) {
    frame_diag_initialized_ = true;
    frame_diag_enabled_ = envFlagEnabled(std::getenv("KARMA_ENGINE_FRAME_DIAG"));
    frame_diag_threshold_ms_ =
        std::max(0.0f, envFloat(std::getenv("KARMA_ENGINE_FRAME_DIAG_THRESHOLD_MS"),
                                frame_diag_threshold_ms_));
    if (frame_diag_enabled_) {
      spdlog::info("KARMA_ENGINE_FRAME_DIAG enabled; format=events_v3 logging frames >= {:.2f} ms",
                   frame_diag_threshold_ms_);
    }
  }

  const double frame_pace_ms = applyFramePacing();
  const auto tick_start = core::SteadyClock::now();
  const auto now = core::SteadyClock::now();
  const float raw_frame_dt = core::elapsedSeconds(last_time_, now);
  float frame_dt = raw_frame_dt;
  if (frame_dt > config_.max_frame_dt) {
    frame_dt = config_.max_frame_dt;
  }
  last_time_ = now;
  accumulator_ += frame_dt;

  auto section_start = core::SteadyClock::now();
  double poll_events_ms = 0.0;
  double ui_events_ms = 0.0;
  double input_update_ms = 0.0;
  double clear_events_ms = 0.0;
  double should_close_ms = 0.0;
  size_t event_count = 0;
  size_t mouse_button_events = 0;
  size_t mouse_move_events = 0;
  size_t window_focus_events = 0;
  size_t window_resize_events = 0;
  if (window_) {
    auto event_section_start = section_start;
    window_->pollEvents();
    const auto& polled_events = window_->events();
    const bool window_close_event =
        std::any_of(polled_events.begin(), polled_events.end(), [](const platform::Event& event) {
          return event.type == platform::EventType::WindowClose;
        });
    if (window_close_event || window_->shouldClose()) {
      requestStop();
      window_->clearEvents();
      shutdownRunningGame();
      return;
    }
    auto event_section_end = core::SteadyClock::now();
    poll_events_ms = core::elapsedMilliseconds(event_section_start, event_section_end);

    const auto& events = polled_events;
    event_count = events.size();
    for (const auto& event : events) {
      switch (event.type) {
        case platform::EventType::MouseButtonDown:
        case platform::EventType::MouseButtonUp:
          ++mouse_button_events;
          break;
        case platform::EventType::MouseMove:
          ++mouse_move_events;
          break;
        case platform::EventType::WindowFocus:
          ++window_focus_events;
          break;
        case platform::EventType::WindowResize:
          ++window_resize_events;
          break;
        default:
          break;
      }
    }

    if (mouse_button_events > 0u) {
      last_mouse_button_frame_tick_ = frame_tick;
    }

    event_section_start = event_section_end;
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
    event_section_end = core::SteadyClock::now();
    ui_events_ms = core::elapsedMilliseconds(event_section_start, event_section_end);

    event_section_start = event_section_end;
    input_.update(events);
    event_section_end = core::SteadyClock::now();
    input_update_ms = core::elapsedMilliseconds(event_section_start, event_section_end);

    event_section_start = event_section_end;
    window_->clearEvents();
    event_section_end = core::SteadyClock::now();
    clear_events_ms = core::elapsedMilliseconds(event_section_start, event_section_end);

    event_section_start = event_section_end;
    if (window_->shouldClose()) {
      requestStop();
    }
    event_section_end = core::SteadyClock::now();
    should_close_ms = core::elapsedMilliseconds(event_section_start, event_section_end);
  }
  auto section_end = core::SteadyClock::now();
  const double events_ms = core::elapsedMilliseconds(section_start, section_end);
  const int64_t mouse_button_event_age =
      last_mouse_button_frame_tick_ == std::numeric_limits<uint64_t>::max()
          ? -1
          : static_cast<int64_t>(frame_tick - last_mouse_button_frame_tick_);
  const bool skip_present_this_frame =
      config_.skip_present_on_mouse_button &&
      mouse_button_event_age >= 0 &&
      mouse_button_event_age <
          static_cast<int64_t>(config_.mouse_button_present_skip_frames);

  if (!running_) {
    shutdownRunningGame();
    return;
  }

  int fixed_steps = 0;
  section_start = section_end;
  for (auto& module : runtime_modules_) {
    if (module) {
      module->onFrameBegin(world_, frame_dt);
    }
  }
  while (accumulator_ >= fixed_dt_) {
    for (auto& module : runtime_modules_) {
      if (module) {
        module->onBeforeFixedUpdate(world_, fixed_dt_, fixed_tick_);
      }
    }
    game_->onFixedUpdate(fixed_dt_);
    // Physics runs via SystemGraph.
    systems_.update(world_, fixed_dt_);
    game_->onPostFixedUpdate(fixed_dt_);
    for (auto& module : runtime_modules_) {
      if (module) {
        module->onAfterFixedUpdate(world_, fixed_dt_, fixed_tick_);
      }
    }
    accumulator_ -= fixed_dt_;
    ++fixed_steps;
    ++fixed_tick_;
  }
  section_end = core::SteadyClock::now();
  const double fixed_ms = core::elapsedMilliseconds(section_start, section_end);

  float render_alpha = 1.0f;
  if (fixed_dt_ > 0.0f) {
    render_alpha = std::clamp(accumulator_ / fixed_dt_, 0.0f, 1.0f);
  }
  game_->render_interpolation_alpha_ = render_alpha;

  section_start = section_end;
  game_->onUpdate(frame_dt);
  section_end = core::SteadyClock::now();
  const double game_update_ms = core::elapsedMilliseconds(section_start, section_end);
  if (!running_) {
    shutdownRunningGame();
    return;
  }

  section_start = section_end;
  light_pulse_system_->update(world_, frame_dt);
  section_end = core::SteadyClock::now();
  const double light_pulse_ms = core::elapsedMilliseconds(section_start, section_end);

  section_start = section_end;
  syncSceneEntities();
  section_end = core::SteadyClock::now();
  const double sync_scene_ms = core::elapsedMilliseconds(section_start, section_end);

  section_start = section_end;
  animation_system_.update(world_, scene_, frame_dt);
  section_end = core::SteadyClock::now();
  const double animation_ms = core::elapsedMilliseconds(section_start, section_end);

  section_start = section_end;
  world::updateWorldTransforms(world_, scene_);
  section_end = core::SteadyClock::now();
  const double scene_transforms_ms = core::elapsedMilliseconds(section_start, section_end);

  section_start = section_end;
  if (graphics_) {
    deformation_system_.update(world_, scene_, *graphics_, &assets_);
  }
  section_end = core::SteadyClock::now();
  const double mesh_deformation_ms = core::elapsedMilliseconds(section_start, section_end);

  section_start = section_end;
  if (audio_system_) {
    audio_system_->update(world_, frame_dt);
  }
  section_end = core::SteadyClock::now();
  const double audio_ms = core::elapsedMilliseconds(section_start, section_end);

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
    section_end = core::SteadyClock::now();
    framebuffer_ms = core::elapsedMilliseconds(section_start, section_end);

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
    section_end = core::SteadyClock::now();
    ui_frame_ms = core::elapsedMilliseconds(section_start, section_end);
    if (!running_) {
      shutdownRunningGame();
      return;
    }

    rendering::FrameInfo frame{};
    frame.width = fb_width;
    frame.height = fb_height;
    frame.delta_time = frame_dt;
    frame.present = !skip_present_this_frame;

    section_start = section_end;
    graphics_->beginFrame(frame);
    section_end = core::SteadyClock::now();
    begin_frame_ms = core::elapsedMilliseconds(section_start, section_end);

    section_start = section_end;
    if (particle_system_) {
      particle_system_->update(world_, frame_dt, render_alpha);
    }
    section_end = core::SteadyClock::now();
    particles_ms = core::elapsedMilliseconds(section_start, section_end);

    section_start = section_end;
    for (auto& module : runtime_modules_) {
      if (module) {
        module->onUpdate(world_, frame_dt, render_alpha);
      }
    }
    section_end = core::SteadyClock::now();
    runtime_modules_ms = core::elapsedMilliseconds(section_start, section_end);

    section_start = section_end;
    render_system_->update(world_, scene_, frame_dt, render_alpha);
    section_end = core::SteadyClock::now();
    render_system_ms = core::elapsedMilliseconds(section_start, section_end);

    section_start = section_end;
    if (user_ui_) {
      graphics_->renderUi(user_ui_context_.draw_data_);
    }
#if defined(KARMA_DEBUG_UI)
    if (debug_ui_) {
      graphics_->renderUi(debug_ui_context_.draw_data_);
    }
#endif
    section_end = core::SteadyClock::now();
    render_ui_ms = core::elapsedMilliseconds(section_start, section_end);

    section_start = section_end;
    graphics_->endFrame();
    section_end = core::SteadyClock::now();
    end_frame_ms = core::elapsedMilliseconds(section_start, section_end);

    section_start = section_end;
    if (window_) {
#if !defined(KARMA_RENDER_BACKEND_DILIGENT)
      window_->swapBuffers();
#endif
    }
    section_end = core::SteadyClock::now();
    swap_buffers_ms = core::elapsedMilliseconds(section_start, section_end);
  }

  for (auto& module : runtime_modules_) {
    if (module) {
      module->onFrameEnd(world_);
    }
  }

  const auto tick_end = core::SteadyClock::now();
  const double tick_total_ms = core::elapsedMilliseconds(tick_start, tick_end);
  const double raw_frame_ms = static_cast<double>(raw_frame_dt) * 1000.0;
  rendering::RendererFrameTimingStats frame_timing_stats{};
  if (frame_diag_enabled_ && graphics_) {
    frame_timing_stats = graphics_->getRendererFrameTimingStats();
  }
  if (frame_diag_enabled_ &&
      (raw_frame_ms >= static_cast<double>(frame_diag_threshold_ms_) ||
       tick_total_ms >= static_cast<double>(frame_diag_threshold_ms_) ||
       frame_timing_stats.render_thread_frame_ms >= frame_diag_threshold_ms_ ||
       frame_timing_stats.swapchain_present_ms >= frame_diag_threshold_ms_ ||
       skip_present_this_frame)) {
    rendering::InstancingStats instancing_stats{};
    rendering::RendererCommandStats command_stats{};
    rendering::ForwardPlusStats forward_plus_stats{};
    if (graphics_) {
      instancing_stats = graphics_->getInstancingStats();
      command_stats = graphics_->getRendererCommandStats();
      forward_plus_stats = graphics_->getForwardPlusStats();
    }
    const uint32_t command_draws =
        command_stats.draw + command_stats.draw_indexed +
        command_stats.draw_indirect + command_stats.draw_indexed_indirect +
        command_stats.multi_draw + command_stats.multi_draw_indexed;
    spdlog::info(
        "Engine frame diag: raw_dt={:.3f}ms clamped_dt={:.3f}ms tick={:.3f}ms pace={:.3f}ms "
        "events={:.3f}[poll={:.3f} ui={:.3f} input={:.3f} clear={:.3f} close={:.3f} "
        "count={} mb={} mb_age={} mm={} focus={} resize={} skip_present={}] "
        "fixed={:.3f}({}) game={:.3f} light_pulse={:.3f} "
        "sync_scene={:.3f} animation={:.3f} scene_xform={:.3f} mesh_deform={:.3f} audio={:.3f} "
        "fb={:.3f} ui_frame={:.3f} begin={:.3f} particles={:.3f} modules={:.3f} "
        "render_system={:.3f} render_layer={:.3f} render_ui={:.3f} end_frame={:.3f} "
        "swap={:.3f} alpha={:.3f} accumulator={:.3f} "
        "renderer_backend=[submitted={} completed={} dropped={} queue={} "
        "record={:.3f} submit={:.3f} rt_wait={:.3f} rt_frame={:.3f} cmd_wait={:.3f} "
        "layers={} draws={} layer={:.3f} post={:.3f} present_copy={:.3f} "
        "present={:.3f} skipped_present={} skip_flush={:.3f} ui={:.3f} "
        "stages=[target={:.3f} clear={:.3f} camera={:.3f} env={:.3f} fplus={:.3f} "
        "shadow={:.3f} terrain={:.3f} collect={:.3f} opaque={:.3f} transparent={:.3f} "
        "particle_res={:.3f} particle={:.3f} line_res={:.3f} line={:.3f}] "
        "resize={}:{:.3f} res_create={}:{:.3f} pipeline_create={}:{:.3f}] "
        "inst=[submitted={} drawn={} culled_batches={} draw_calls={} uploads={} bytes={} "
        "gpu_cull=[batches={} dispatch={} candidates={} indirect={}] "
        "lod=[buckets={} dispatch={} candidates={} indirect={} fallback={}] "
        "extract={:.3f} collect={:.3f} upload={:.3f}] "
        "cmd_totals=[draws={} update_buffer={} copy_texture={} dispatch={}] "
        "forward_plus=[active={} cpu={} lights={} tiles={}x{} overflow={}]",
        raw_frame_ms,
        static_cast<double>(frame_dt) * 1000.0,
        tick_total_ms,
        frame_pace_ms,
        events_ms,
        poll_events_ms,
        ui_events_ms,
        input_update_ms,
        clear_events_ms,
        should_close_ms,
        event_count,
        mouse_button_events,
        mouse_button_event_age,
        mouse_move_events,
        window_focus_events,
        window_resize_events,
        skip_present_this_frame ? 1 : 0,
        fixed_ms,
        fixed_steps,
        game_update_ms,
        light_pulse_ms,
        sync_scene_ms,
        animation_ms,
        scene_transforms_ms,
        mesh_deformation_ms,
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
        accumulator_,
        frame_timing_stats.submitted_frames,
        frame_timing_stats.completed_frames,
        frame_timing_stats.dropped_frames,
        frame_timing_stats.render_queue_depth,
        frame_timing_stats.frame_record_ms,
        frame_timing_stats.frame_submit_ms,
        frame_timing_stats.render_thread_wait_ms,
        frame_timing_stats.render_thread_frame_ms,
        frame_timing_stats.render_thread_command_wait_ms,
        frame_timing_stats.render_layer_count,
        frame_timing_stats.render_layer_draws,
        frame_timing_stats.render_layer_total_ms,
        frame_timing_stats.post_process_ms,
        frame_timing_stats.present_copy_ms,
        frame_timing_stats.swapchain_present_ms,
        frame_timing_stats.skipped_presents,
        frame_timing_stats.skipped_present_flush_ms,
        frame_timing_stats.render_ui_ms,
        frame_timing_stats.target_setup_ms,
        frame_timing_stats.clear_ms,
        frame_timing_stats.camera_setup_ms,
        frame_timing_stats.environment_ms,
        frame_timing_stats.forward_plus_ms,
        frame_timing_stats.shadow_ms,
        frame_timing_stats.terrain_ms,
        frame_timing_stats.forward_collect_ms,
        frame_timing_stats.opaque_ms,
        frame_timing_stats.transparent_ms,
        frame_timing_stats.particle_resources_ms,
        frame_timing_stats.particle_pass_ms,
        frame_timing_stats.line_resources_ms,
        frame_timing_stats.line_draw_ms,
        frame_timing_stats.resize_events,
        frame_timing_stats.resize_ms,
        frame_timing_stats.resource_creation_count,
        frame_timing_stats.resource_creation_ms,
        frame_timing_stats.pipeline_creation_count,
        frame_timing_stats.pipeline_creation_ms,
        instancing_stats.submitted_instances,
        instancing_stats.drawn_instances,
        instancing_stats.culled_batches,
        instancing_stats.draw_calls,
        instancing_stats.instance_buffer_updates,
        instancing_stats.instance_upload_bytes,
        instancing_stats.gpu_culling_batches,
        instancing_stats.gpu_culling_dispatches,
        instancing_stats.gpu_culling_candidate_instances,
        instancing_stats.gpu_indirect_draws,
        instancing_stats.lod_bucket_count,
        instancing_stats.lod_culling_dispatches,
        instancing_stats.lod_candidate_instances,
        instancing_stats.lod_indirect_draws,
        instancing_stats.lod_fallbacks,
        instancing_stats.render_system_extraction_ms,
        instancing_stats.forward_state_collection_ms,
        instancing_stats.instance_upload_ms,
        command_draws,
        command_stats.update_buffer,
        command_stats.copy_texture,
        command_stats.dispatch_compute,
        forward_plus_stats.active,
        forward_plus_stats.cpu_fallback,
        forward_plus_stats.local_light_count,
        forward_plus_stats.tiles_x,
        forward_plus_stats.tiles_y,
        forward_plus_stats.overflow_risk);
  }

  if (!running_) {
    shutdownRunningGame();
  }
}

}  // namespace karma::app
