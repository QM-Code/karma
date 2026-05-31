#pragma once

#include "karma/rendering/renderer/backend.hpp"
#include "karma/rendering/renderer/material.h"

namespace karma::renderer {

class GraphicsDevice {
 public:
  explicit GraphicsDevice(karma::platform::Window& window);
  ~GraphicsDevice();

  void beginFrame(const FrameInfo& frame);
  void endFrame();
  void resize(int width, int height);
  void getFramebufferSize(int& width, int& height) const;

  MeshId createMesh(const MeshData& mesh);
  void updateMesh(MeshId mesh, const MeshData& data);
  MeshId createMeshFromFile(const std::filesystem::path& path);
  void destroyMesh(MeshId mesh);
  bool getMeshBounds(MeshId mesh, glm::vec3& center, float& radius) const;

  MaterialId createMaterial(const MaterialDesc& material);
  MaterialId createMaterialFromAsset(const std::filesystem::path& path, uint32_t material_index);
  void updateMaterial(MaterialId material, const MaterialDesc& desc);
  void destroyMaterial(MaterialId material);
  MaterialSetId createMaterialSetFromMesh(MeshId mesh, const MaterialResourceDesc& desc);
  void destroyMaterialSet(MaterialSetId set);
  void setMaterialFloat(MaterialId material, std::string_view name, float value);

  TextureId createTexture(const TextureDesc& desc);
  void destroyTexture(TextureId texture);

  RenderTargetId createRenderTarget(const RenderTargetDesc& desc);
  void destroyRenderTarget(RenderTargetId target);

  void submit(const DrawItem& item);
  void submitParticles(ParticleBatch batch);
  void submitPackedParticles(PackedParticleBatch batch);
  void setParticleSystemStats(const ParticlePassStats& stats);
  void retireInstance(InstanceId instance);
  void renderLayer(LayerId layer, RenderTargetId target = kDefaultRenderTarget);
  void drawLine(const math::Vec3& start, const math::Vec3& end, const math::Color& color,
                bool depth_test = true, float thickness = 1.0f);

  unsigned int getRenderTargetTextureId(RenderTargetId target) const;

  void setCamera(const CameraData& camera);
  void setCameraActive(bool active);
  void setDirectionalLight(const DirectionalLightData& light);
  void setLights(const std::vector<LightData>& lights);
  void setEnvironmentMap(const std::filesystem::path& path, float intensity, bool draw_skybox);
  void setVsync(bool enabled);
  void setAnisotropy(bool enabled, int level);
  void setGenerateMips(bool enabled);
  void setForwardPlusSettings(int tile_size, int max_lights_per_tile, int max_local_lights);
  ForwardPlusStats getForwardPlusStats() const;
  ParticlePassStats getParticlePassStats() const;
  void setShadowSettings(float bias,
                         int map_size,
                         int pcf_radius,
                         int raster_depth_bias,
                         float raster_slope_bias,
                         float receiver_bias_scale,
                         float normal_bias_scale);
  void setPointShadowSettings(float constant_bias,
                              float slope_bias_scale,
                              float normal_bias_scale,
                              float receiver_bias_scale);
  void setPointShadowLightLimit(int max_lights);
  void setLocalLightingSettings(float distance_damping,
                                float range_falloff_exponent,
                                bool ao_affects_local_lights,
                                float directional_shadow_lift_strength);
  void setExposure(float exposure);
  TextureId createTextureRGBA8(int width, int height, const void* pixels);
  void updateTextureRGBA8(TextureId texture, int width, int height, const void* pixels);
  void renderUi(const karma::app::UIDrawData& draw_data);
  renderer_backend::Backend* backend() { return backend_.get(); }
  const renderer_backend::Backend* backend() const { return backend_.get(); }

 private:
  std::unique_ptr<renderer_backend::Backend> backend_;
  int framebuffer_width_ = 0;
  int framebuffer_height_ = 0;
};

}  // namespace karma::renderer
