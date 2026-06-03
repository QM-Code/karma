#pragma once

#include "karma/rendering/renderer/backend.hpp"
#include "karma/rendering/renderer/material.h"

namespace karma::renderer {

/// \ingroup karma_rendering
/// High-level renderer facade owned by `EngineApp`.
///
/// `GraphicsDevice` forwards resource creation, scene submission, UI rendering,
/// diagnostics, and frame lifecycle calls to the configured backend. Game code
/// generally reaches it through `GameInterface::graphics`.
class GraphicsDevice {
 public:
  /// Creates the configured graphics backend for `window`.
  explicit GraphicsDevice(karma::platform::Window& window);
  ~GraphicsDevice();

  /// Begins a frame with viewport/timing data.
  void beginFrame(const FrameInfo& frame);
  /// Ends and presents the current frame.
  void endFrame();
  /// Resizes backend swapchain/framebuffer resources.
  void resize(int width, int height);
  /// Returns the current framebuffer size.
  void getFramebufferSize(int& width, int& height) const;

  /// Uploads CPU mesh data and returns a mesh handle.
  MeshId createMesh(const MeshData& mesh);
  /// Replaces mesh data for an existing handle.
  void updateMesh(MeshId mesh, const MeshData& data);
  /// Loads and uploads mesh data from a file.
  MeshId createMeshFromFile(const std::filesystem::path& path);
  /// Destroys a mesh handle.
  void destroyMesh(MeshId mesh);
  /// Queries cached mesh bounds.
  bool getMeshBounds(MeshId mesh, glm::vec3& center, float& radius) const;

  /// Creates a material from explicit parameters.
  MaterialId createMaterial(const MaterialDesc& material);
  /// Creates a material from an imported asset material index.
  MaterialId createMaterialFromAsset(const std::filesystem::path& path, uint32_t material_index);
  /// Updates material parameters.
  void updateMaterial(MaterialId material, const MaterialDesc& desc);
  /// Destroys a material.
  void destroyMaterial(MaterialId material);
  /// Creates a material set for a mesh asset.
  MaterialSetId createMaterialSetFromMesh(MeshId mesh, const MaterialResourceDesc& desc);
  /// Destroys a material set.
  void destroyMaterialSet(MaterialSetId set);
  /// Sets a named float parameter on a material when supported.
  void setMaterialFloat(MaterialId material, std::string_view name, float value);

  /// Creates a texture from descriptor data.
  TextureId createTexture(const TextureDesc& desc);
  /// Destroys a texture.
  void destroyTexture(TextureId texture);

  /// Creates a render target.
  RenderTargetId createRenderTarget(const RenderTargetDesc& desc);
  /// Destroys a render target.
  void destroyRenderTarget(RenderTargetId target);

  /// Submits one mesh draw item.
  void submit(const DrawItem& item);
  /// Submits a compatibility particle batch.
  void submitParticles(ParticleBatch batch);
  /// Submits a packed particle batch.
  void submitPackedParticles(PackedParticleBatch batch);
  /// Provides particle-system timings/counters to the renderer.
  void setParticleSystemStats(const ParticlePassStats& stats);
  /// Retires a renderer instance id.
  void retireInstance(InstanceId instance);
  /// Renders one layer into a target.
  void renderLayer(LayerId layer, RenderTargetId target = kDefaultRenderTarget);
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
  /// Returns latest particle-pass diagnostics.
  ParticlePassStats getParticlePassStats() const;
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
  void renderUi(const karma::app::UIDrawData& draw_data);
  /// Returns the backend implementation for narrow diagnostics/interops.
  renderer_backend::Backend* backend() { return backend_.get(); }
  /// Returns the backend implementation for narrow diagnostics/interops.
  const renderer_backend::Backend* backend() const { return backend_.get(); }

 private:
  std::unique_ptr<renderer_backend::Backend> backend_;
  int framebuffer_width_ = 0;
  int framebuffer_height_ = 0;
};

}  // namespace karma::renderer
