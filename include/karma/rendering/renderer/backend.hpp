#pragma once

#include "karma/core/math/types.h"
#include "karma/rendering/renderer/camera.h"
#include "karma/rendering/renderer/draw_item.h"
#include "karma/rendering/renderer/frame.h"
#include "karma/rendering/renderer/ids.h"
#include "karma/rendering/renderer/lights.h"
#include "karma/rendering/renderer/material.h"
#include "karma/world/geometry/mesh_data.h"
#include "karma/rendering/renderer/particles.h"
#include "karma/rendering/renderer/post_process.h"
#include "karma/rendering/renderer/render_target.h"
#include "karma/rendering/renderer/stats.h"
#include "karma/rendering/renderer/texture.h"
#include "karma/rendering/renderer/ui_draw_data.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace karma::platform {
class Window;
}

namespace karma::renderer_backend {

/// \ingroup karma_rendering
/// Renderer backend interface implemented by platform graphics backends.
///
/// `GraphicsDevice` owns an implementation and exposes the same operations to
/// runtime code. Backends should keep API resources opaque and validate handles
/// defensively. Post-process state is supplied per `renderLayer` call after
/// cameras resolve profile intent through `RenderSystem`.
class Backend {
 public:
  virtual ~Backend() = default;

  virtual void beginFrame(const renderer::FrameInfo& frame) = 0;
  virtual void endFrame() = 0;
  virtual void resize(int width, int height) = 0;

  virtual renderer::MeshId createMesh(const geometry::MeshData& mesh) = 0;
  virtual void updateMesh(renderer::MeshId mesh, const geometry::MeshData& data) = 0;
  virtual renderer::MeshId createMeshFromFile(const std::filesystem::path& path) = 0;
  virtual void destroyMesh(renderer::MeshId mesh) = 0;
  virtual bool getMeshBounds(renderer::MeshId mesh, glm::vec3& center, float& radius) const = 0;

  virtual renderer::MaterialId createMaterial(const renderer::MaterialDesc& material) = 0;
  virtual renderer::MaterialId createMaterialFromAsset(const std::filesystem::path& path,
                                                       uint32_t material_index) = 0;
  virtual void updateMaterial(renderer::MaterialId material, const renderer::MaterialDesc& desc) = 0;
  virtual void destroyMaterial(renderer::MaterialId material) = 0;
  virtual renderer::MaterialSetId createMaterialSetFromMesh(renderer::MeshId mesh,
                                                            const renderer::MaterialResourceDesc& desc) = 0;
  virtual void destroyMaterialSet(renderer::MaterialSetId set) = 0;
  virtual void setMaterialFloat(renderer::MaterialId material, std::string_view name, float value) = 0;

  virtual renderer::TextureId createTexture(const renderer::TextureDesc& desc) = 0;
  virtual void destroyTexture(renderer::TextureId texture) = 0;

  virtual renderer::RenderTargetId createRenderTarget(const renderer::RenderTargetDesc& desc) = 0;
  virtual void destroyRenderTarget(renderer::RenderTargetId target) = 0;

  virtual void submit(const renderer::DrawItem& item) = 0;
  virtual void submitParticles(renderer::ParticleBatch batch) = 0;
  virtual void submitPackedParticles(renderer::PackedParticleBatch batch) = 0;
  virtual void submitParticleEmitter(const renderer::ParticleEmitterGpuDesc& emitter) = 0;
  virtual void setParticleSystemStats(const renderer::ParticlePassStats& stats) = 0;
  virtual void retireInstance(renderer::InstanceId instance) = 0;
  /// Renders one extracted layer into a target using per-pass post settings.
  virtual void renderLayer(renderer::LayerId layer,
                           renderer::RenderTargetId target,
                           const renderer::PostProcessSettings& post_process) = 0;
  virtual void drawLine(const math::Vec3& start, const math::Vec3& end,
                        const math::Color& color, bool depth_test, float thickness) = 0;

  virtual unsigned int getRenderTargetTextureId(renderer::RenderTargetId target) const = 0;

  virtual void setCamera(const renderer::CameraData& camera) = 0;
  virtual void setCameraActive(bool active) = 0;
  virtual void setDirectionalLight(const renderer::DirectionalLightData& light) = 0;
  virtual void setLights(const std::vector<renderer::LightData>& lights) = 0;
  virtual void setEnvironmentMap(const std::filesystem::path& path, float intensity,
                                 bool draw_skybox) = 0;
  virtual void setVsync(bool enabled) = 0;
  virtual void setAnisotropy(bool enabled, int level) = 0;
  virtual void setGenerateMips(bool enabled) = 0;
  virtual void setForwardPlusSettings(int tile_size,
                                      int max_lights_per_tile,
                                      int max_local_lights) = 0;
  virtual renderer::ForwardPlusStats getForwardPlusStats() const = 0;
  virtual renderer::ParticlePassStats getParticlePassStats() const = 0;
  virtual renderer::RendererCommandStats getRendererCommandStats() const = 0;
  virtual void setShadowSettings(float bias,
                                 int map_size,
                                 int pcf_radius,
                                 int raster_depth_bias,
                                 float raster_slope_bias,
                                 float receiver_bias_scale,
                                 float normal_bias_scale) = 0;
  virtual void setPointShadowSettings(float constant_bias,
                                      float slope_bias_scale,
                                      float normal_bias_scale,
                                      float receiver_bias_scale) = 0;
  virtual void setPointShadowLightLimit(int max_lights) = 0;
  virtual void setLocalLightingSettings(float distance_damping,
                                        float range_falloff_exponent,
                                        bool ao_affects_local_lights,
                                        float directional_shadow_lift_strength) = 0;
  virtual void setExposure(float exposure) = 0;

  virtual void updateTextureRGBA8(renderer::TextureId texture, int w, int h, const void* pixels) = 0;
  virtual void renderUi(const karma::renderer::UIDrawData& draw_data) = 0;
};

/// Creates the configured graphics backend for a platform window.
std::unique_ptr<Backend> CreateGraphicsBackend(karma::platform::Window& window);

}  // namespace karma::renderer_backend
