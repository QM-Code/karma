#pragma once

#include "karma/math.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/world.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"
#include "karma/rendering.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace karma::platform {
class Window;
}

namespace karma::rendering::backend {

/// \ingroup karma_rendering
/// Renderer backend interface implemented by platform graphics backends.
///
/// `GraphicsDevice` owns an implementation and exposes the same operations to
/// runtime code. Backends should keep API resources opaque and validate handles
/// defensively. Frame graphs are supplied per `renderLayer` call after cameras
/// resolve graph intent through `RenderSystem`.
class Backend {
 public:
  virtual ~Backend() = default;

  /// Returns whether device/context and presentation resources initialized successfully.
  virtual bool isValid() const { return true; }

  virtual void beginFrame(const rendering::FrameInfo& frame) = 0;
  virtual void endFrame() = 0;
  virtual void resize(int width, int height) = 0;
  /// Creates renderer variants that are normally lazy but expected in the
  /// startup-visible scene.
  virtual void prewarmRendererResources(bool include_ui) { (void)include_ui; }
  /// Allows backends with explicit render-state caches to persist warm-up work.
  virtual void flushRenderStateCache() {}

  virtual rendering::MeshId createMesh(const world::MeshData& mesh) = 0;
  virtual void updateMesh(rendering::MeshId mesh, const world::MeshData& data) = 0;
  virtual void destroyMesh(rendering::MeshId mesh) = 0;
  virtual bool getMeshBounds(rendering::MeshId mesh, glm::vec3& center, float& radius) const = 0;

  virtual bool getMeshMaterialSlots(rendering::MeshId mesh,
                                    std::vector<world::MeshMaterialSlot>& out_slots) const = 0;

  virtual rendering::MaterialId createMaterial(const rendering::ResolvedMaterialDesc& material) = 0;
  virtual rendering::MaterialId createMaterialFromAsset(const std::filesystem::path& path,
                                                       uint32_t material_index) = 0;
  virtual void updateMaterial(rendering::MaterialId material, const rendering::MaterialDesc& desc) = 0;
  virtual void destroyMaterial(rendering::MaterialId material) = 0;
  virtual void setMaterialFloat(rendering::MaterialId material, std::string_view name, float value) = 0;

  virtual rendering::TextureId createTexture(const rendering::TextureDesc& desc) = 0;
  virtual bool supportsTextureFormat(rendering::TextureFormat format) const {
    switch (format) {
      case rendering::TextureFormat::RGBA8:
      case rendering::TextureFormat::RGB8:
      case rendering::TextureFormat::R8:
        return true;
      case rendering::TextureFormat::BC7_RGBA_UNORM:
      case rendering::TextureFormat::BC7_RGBA_UNORM_SRGB:
      case rendering::TextureFormat::KTX2_BASIS_UASTC:
        return false;
    }
    return false;
  }
  virtual bool uploadTexture(rendering::TextureId texture,
                             const rendering::TextureUploadData& upload) {
    (void)texture;
    (void)upload;
    return false;
  }
  virtual void destroyTexture(rendering::TextureId texture) = 0;

  virtual rendering::RenderTargetId createRenderTarget(const rendering::RenderTargetDesc& desc) = 0;
  virtual void destroyRenderTarget(rendering::RenderTargetId target) = 0;

  virtual rendering::TerrainId createTerrain(const rendering::TerrainDesc& desc) {
    (void)desc;
    return rendering::kInvalidTerrain;
  }
  virtual void destroyTerrain(rendering::TerrainId terrain) { (void)terrain; }
  virtual void uploadTerrainTile(rendering::TerrainId terrain,
                                 const rendering::TerrainTileData& tile) {
    (void)terrain;
    (void)tile;
  }
  virtual void uploadTerrainMaterialLayer(
      rendering::TerrainId terrain,
      const rendering::TerrainMaterialLayerData& layer) {
    (void)terrain;
    (void)layer;
  }
  virtual void clearTerrainMaterialLayers(rendering::TerrainId terrain) {
    (void)terrain;
  }
  virtual void evictTerrainTile(rendering::TerrainId terrain,
                                rendering::TerrainTileCoord coord) {
    (void)terrain;
    (void)coord;
  }
  virtual void submitTerrain(const rendering::TerrainDrawItem& item) { (void)item; }
  virtual rendering::TerrainCapabilities getTerrainCapabilities() const { return {}; }
  virtual rendering::TerrainStats getTerrainStats() const { return {}; }

  virtual rendering::DeformationId createDeformation(const rendering::DeformationDesc& desc) {
    (void)desc;
    return rendering::kInvalidDeformation;
  }
  virtual void updateDeformation(rendering::DeformationId deformation,
                                 const rendering::DeformationDesc& desc) {
    (void)deformation;
    (void)desc;
  }
  virtual void destroyDeformation(rendering::DeformationId deformation) {
    (void)deformation;
  }
  virtual rendering::DeformationStats getDeformationStats() const { return {}; }

  virtual void submit(const rendering::DrawItem& item) = 0;
  virtual void submitInstanced(const rendering::InstancedDrawItem& item) = 0;
  virtual void submitParticles(rendering::ParticleBatch batch) = 0;
  virtual void submitPackedParticles(rendering::PackedParticleBatch batch) = 0;
  virtual void submitParticleEmitter(const rendering::ParticleEmitterGpuDesc& emitter) = 0;
  virtual void submitParticleBeam(const rendering::ParticleBeamGpuDesc& beam) = 0;
  virtual void setParticleSystemStats(const rendering::ParticlePassStats& stats) = 0;
  virtual void retireInstance(rendering::InstanceId instance) = 0;
  /// Renders one extracted layer into a target using the resolved frame graph.
  virtual void renderLayer(rendering::LayerId layer,
                           rendering::RenderTargetId target,
                           const rendering::FrameGraphDesc& frame_graph) = 0;
  virtual void drawLine(const math::Vec3& start, const math::Vec3& end,
                        const math::Color& color, bool depth_test, float thickness) = 0;

  virtual unsigned int getRenderTargetTextureId(rendering::RenderTargetId target) const = 0;

  virtual void setCamera(const rendering::CameraData& camera) = 0;
  virtual void setCameraActive(bool active) = 0;
  virtual void setDirectionalLight(const rendering::DirectionalLightData& light) = 0;
  virtual void setLights(const std::vector<rendering::LightData>& lights) = 0;
  virtual void setEnvironmentMap(const std::filesystem::path& path, float intensity,
                                 bool draw_skybox) = 0;
  virtual void setClearColor(const math::Color& color) = 0;
  virtual void setVsync(bool enabled) = 0;
  virtual void setAnisotropy(bool enabled, int level) = 0;
  virtual void setGenerateMips(bool enabled) = 0;
  virtual void setForwardPlusSettings(int tile_size,
                                      int max_lights_per_tile,
                                      int max_local_lights) = 0;
  virtual rendering::ForwardPlusStats getForwardPlusStats() const = 0;
  virtual void setInstancingCpuTimings(float render_system_extraction_ms,
                                       float forward_state_collection_ms) {
    (void)render_system_extraction_ms;
    (void)forward_state_collection_ms;
  }
  virtual rendering::InstancingStats getInstancingStats() const = 0;
  virtual rendering::ParticlePassStats getParticlePassStats() const = 0;
  virtual rendering::RendererCommandStats getRendererCommandStats() const = 0;
  virtual rendering::RendererFrameTimingStats getRendererFrameTimingStats() const = 0;
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

  virtual void updateTextureRGBA8(rendering::TextureId texture, int w, int h, const void* pixels) = 0;
  virtual void renderUi(const karma::rendering::UIDrawData& draw_data) = 0;
};

/// Creates the configured graphics backend for a platform window.
std::unique_ptr<Backend> CreateGraphicsBackend(
    karma::platform::Window& window,
    const rendering::GraphicsDeviceCreateInfo& create_info = {});

}  // namespace karma::rendering::backend
