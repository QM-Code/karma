#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "karma/rendering/renderer/backend.hpp"
#include "karma/rendering/renderer/material.h"
#include "karma/world/geometry/mesh_data.h"

namespace karma::animation {
class DeformationSystem;
}

namespace karma::particles {
class ParticleSystem;
}

namespace karma::renderer {

class RenderSystem;

/// \ingroup karma_rendering
/// High-level renderer facade owned by `EngineApp`.
///
/// `GraphicsDevice` forwards resource creation, scene submission, UI rendering,
/// diagnostics, and frame lifecycle calls to the configured backend. Game code
/// generally reaches it through `GameInterface::graphics`; ordinary scene
/// rendering is driven by `RenderSystem`.
class GraphicsDevice {
 public:
  /// Creates the configured graphics backend for `window`.
  explicit GraphicsDevice(karma::platform::Window& window,
                          const GraphicsDeviceCreateInfo& create_info = {});
  ~GraphicsDevice();

  /// Begins a frame with viewport/timing data.
  void beginFrame(const FrameInfo& frame);
  /// Ends and presents the current frame.
  void endFrame(bool wait_for_completion = false);
  /// Waits until all queued renderer work has completed.
  void waitIdle();
  /// Resizes backend swapchain/framebuffer resources.
  void resize(int width, int height);
  /// Prewarms lazily-created renderer resources expected by startup scenes.
  void prewarmRendererResources(bool include_ui = false);
  /// Persists backend pipeline/render-state cache work when supported.
  void flushRenderStateCache();
  /// Returns the current framebuffer size.
  void getFramebufferSize(int& width, int& height) const;

  /// Uploads CPU mesh data and returns a mesh handle.
  MeshId createMesh(const geometry::MeshData& mesh);
  /// Replaces mesh data for an existing handle.
  void updateMesh(MeshId mesh, const geometry::MeshData& data);
  /// Destroys a mesh handle.
  void destroyMesh(MeshId mesh);
  /// Queries cached mesh bounds.
  bool getMeshBounds(MeshId mesh, glm::vec3& center, float& radius) const;
  /// Queries mesh material-slot metadata.
  bool getMeshMaterialSlots(MeshId mesh, std::vector<geometry::MeshMaterialSlot>& out_slots) const;

  /// Creates a material from resolved material parameters.
  MaterialId createMaterial(const ResolvedMaterialDesc& material);
  /// Creates a material from explicit surface parameters.
  MaterialId createMaterial(const MaterialDesc& material);
  /// Creates a material from an imported asset material index.
  MaterialId createMaterialFromAsset(const std::filesystem::path& path, uint32_t material_index);
  /// Updates material parameters.
  void updateMaterial(MaterialId material, const MaterialDesc& desc);
  /// Destroys a material.
  void destroyMaterial(MaterialId material);
  /// Sets a named float parameter on a material when supported.
  void setMaterialFloat(MaterialId material, std::string_view name, float value);

  /// Creates a texture from descriptor data.
  TextureId createTexture(const TextureDesc& desc);
  /// Returns true when the active backend can create and sample a texture format.
  bool supportsTextureFormat(TextureFormat format) const;
  /// Uploads prepared texture subresources when the backend supports the format.
  bool uploadTexture(TextureId texture, const TextureUploadData& upload);
  /// Destroys a texture.
  void destroyTexture(TextureId texture);

  /// Creates a render target.
  RenderTargetId createRenderTarget(const RenderTargetDesc& desc);
  /// Destroys a render target.
  void destroyRenderTarget(RenderTargetId target);

  /// Creates a streamed terrain resource.
  TerrainId createTerrain(const TerrainDesc& desc);
  /// Destroys a streamed terrain resource.
  void destroyTerrain(TerrainId terrain);
  /// Uploads or replaces one decoded terrain tile.
  void uploadTerrainTile(TerrainId terrain, const TerrainTileData& tile);
  /// Uploads or replaces one shared repeated terrain material layer.
  void uploadTerrainMaterialLayer(TerrainId terrain, const TerrainMaterialLayerData& layer);
  /// Removes all shared repeated material layers from a terrain resource.
  void clearTerrainMaterialLayers(TerrainId terrain);
  /// Evicts one terrain tile from a streamed terrain resource.
  void evictTerrainTile(TerrainId terrain, TerrainTileCoord coord);
  /// Submits one streamed terrain tile draw.
  void submitTerrain(const TerrainDrawItem& item);
  /// Returns active backend terrain capabilities.
  TerrainCapabilities getTerrainCapabilities() const;
  /// Returns active backend terrain diagnostics.
  TerrainStats getTerrainStats() const;

  /// Creates renderer-owned skin/morph deformation resources.
  DeformationId createDeformation(const DeformationDesc& desc);
  /// Updates renderer-owned skin/morph deformation resources.
  void updateDeformation(DeformationId deformation, const DeformationDesc& desc);
  /// Destroys renderer-owned skin/morph deformation resources.
  void destroyDeformation(DeformationId deformation);
  /// Returns active backend deformation diagnostics.
  DeformationStats getDeformationStats() const;

  /// Submits one mesh draw item.
  void submit(const DrawItem& item);
  /// Submits one shared mesh draw with many instances.
  void submitInstanced(const InstancedDrawItem& item);
  /// Submits a compatibility particle batch.
  void submitParticles(ParticleBatch batch);
  /// Submits a packed particle batch.
  void submitPackedParticles(PackedParticleBatch batch);
  /// Submits a renderer-owned particle emitter descriptor.
  void submitParticleEmitter(const ParticleEmitterGpuDesc& emitter);
  /// Provides particle-system timings/counters to the renderer.
  void setParticleSystemStats(const ParticlePassStats& stats);
  /// Retires a renderer instance id.
  void retireInstance(InstanceId instance);
  /// Renders one layer into a target with resolved post-process settings.
  ///
  /// Normal applications let `RenderSystem` call this after resolving the
  /// active camera profile. Custom render paths must pass the settings for that
  /// specific camera pass; there is no backend-global post-process state API.
  void renderLayer(LayerId layer,
                   RenderTargetId target,
                   const PostProcessSettings& post_process);
  /// Queues a debug line.
  void drawLine(const math::Vec3& start, const math::Vec3& end, const math::Color& color,
                bool depth_test = true, float thickness = 1.0f);

  /// Returns a backend texture id for UI/provider interop when supported.
  unsigned int getRenderTargetTextureId(RenderTargetId target) const;

  /// Sets active camera data.
  void setCamera(const CameraData& camera);
  /// Enables or disables camera-dependent rendering.
  void setCameraActive(bool active);
  /// Sets directional light data.
  void setDirectionalLight(const DirectionalLightData& light);
  /// Sets local light data for Forward+ rendering.
  void setLights(const std::vector<LightData>& lights);
  /// Sets environment map and skybox state.
  void setEnvironmentMap(const std::filesystem::path& path, float intensity, bool draw_skybox);
  /// Sets presentation vsync policy.
  void setVsync(bool enabled);
  /// Enables/disables anisotropic filtering.
  void setAnisotropy(bool enabled, int level);
  /// Enables/disables generated mipmaps for eligible texture uploads.
  void setGenerateMips(bool enabled);
  /// Configures Forward+ local-light limits.
  void setForwardPlusSettings(int tile_size, int max_lights_per_tile, int max_local_lights);
  /// Returns latest Forward+ diagnostics.
  ForwardPlusStats getForwardPlusStats() const;
  /// Updates CPU-side instancing timings measured outside the backend.
  void setInstancingCpuTimings(float render_system_extraction_ms,
                               float forward_state_collection_ms = -1.0f);
  /// Returns latest instanced-rendering diagnostics.
  InstancingStats getInstancingStats() const;
  /// Returns latest particle-pass diagnostics.
  ParticlePassStats getParticlePassStats() const;
  /// Returns renderer backend command counters.
  RendererCommandStats getRendererCommandStats() const;
  /// Returns renderer backend frame timings for the most recently completed frame.
  RendererFrameTimingStats getRendererFrameTimingStats() const;
  /// Configures directional shadow bias/map settings.
  void setShadowSettings(float bias,
                         int map_size,
                         int pcf_radius,
                         int raster_depth_bias,
                         float raster_slope_bias,
                         float receiver_bias_scale,
                         float normal_bias_scale);
  /// Configures point-shadow bias settings.
  void setPointShadowSettings(float constant_bias,
                              float slope_bias_scale,
                              float normal_bias_scale,
                              float receiver_bias_scale);
  /// Sets the runtime point-shadow light budget.
  void setPointShadowLightLimit(int max_lights);
  /// Configures local-light attenuation and shadow interaction.
  void setLocalLightingSettings(float distance_damping,
                                float range_falloff_exponent,
                                bool ao_affects_local_lights,
                                float directional_shadow_lift_strength);
  /// Sets final lighting exposure.
  void setExposure(float exposure);
  /// Creates an RGBA8 texture from raw pixels.
  TextureId createTextureRGBA8(int width, int height, const void* pixels);
  /// Updates an RGBA8 texture from raw pixels.
  void updateTextureRGBA8(TextureId texture, int width, int height, const void* pixels);
  /// Renders provider-neutral UI draw data.
  void renderUi(const karma::renderer::UIDrawData& draw_data);
  /// Returns the backend implementation for narrow diagnostics/interops.
  renderer_backend::Backend* backend();
  /// Returns the backend implementation for narrow diagnostics/interops.
  const renderer_backend::Backend* backend() const;

 private:
  friend class RenderSystem;
  friend class karma::animation::DeformationSystem;
  friend class karma::particles::ParticleSystem;
  class RenderScheduler;

  /// Registers or replaces a runtime mesh backing a content mesh asset.
  MeshId registerRuntimeMesh(const std::string& key, const geometry::MeshData& mesh);
  /// Removes a runtime mesh registration.
  void unregisterRuntimeMesh(const std::string& key);
  /// Returns a registered runtime mesh id, or `kInvalidMesh`.
  MeshId findRuntimeMesh(const std::string& key) const;

  std::unique_ptr<RenderScheduler> scheduler_;
  struct RuntimeMeshRegistration {
    MeshId mesh = kInvalidMesh;
    geometry::MeshData data;
  };
  std::unordered_map<std::string, RuntimeMeshRegistration> runtime_meshes_;
  mutable std::recursive_mutex mutex_;
  int framebuffer_width_ = 0;
  int framebuffer_height_ = 0;
};

using Renderer = GraphicsDevice;

}  // namespace karma::renderer
