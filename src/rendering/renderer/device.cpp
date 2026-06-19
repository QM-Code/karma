#include "karma/rendering/renderer/device.h"

#include <mutex>
#include <utility>

namespace karma::renderer {

GraphicsDevice::GraphicsDevice(karma::platform::Window& window) {
  backend_ = renderer_backend::CreateGraphicsBackend(window);
}

GraphicsDevice::~GraphicsDevice() = default;

void GraphicsDevice::beginFrame(const FrameInfo& frame) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  framebuffer_width_ = frame.width;
  framebuffer_height_ = frame.height;
  if (backend_) {
    backend_->beginFrame(frame);
  }
}

void GraphicsDevice::endFrame() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->endFrame();
  }
}

void GraphicsDevice::resize(int width, int height) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  framebuffer_width_ = width;
  framebuffer_height_ = height;
  if (backend_) {
    backend_->resize(width, height);
  }
}

void GraphicsDevice::getFramebufferSize(int& width, int& height) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  width = framebuffer_width_;
  height = framebuffer_height_;
}

MeshId GraphicsDevice::createMesh(const geometry::MeshData& mesh) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->createMesh(mesh) : kInvalidMesh;
}

void GraphicsDevice::updateMesh(MeshId mesh, const geometry::MeshData& data) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->updateMesh(mesh, data);
  }
}

void GraphicsDevice::destroyMesh(MeshId mesh) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->destroyMesh(mesh);
  }
}

bool GraphicsDevice::getMeshBounds(MeshId mesh, glm::vec3& center, float& radius) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!backend_) {
    return false;
  }
  return backend_->getMeshBounds(mesh, center, radius);
}

bool GraphicsDevice::getMeshMaterialSlots(
    MeshId mesh,
    std::vector<geometry::MeshMaterialSlot>& out_slots) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!backend_) {
    out_slots.clear();
    return false;
  }
  return backend_->getMeshMaterialSlots(mesh, out_slots);
}

MeshId GraphicsDevice::registerRuntimeMesh(const std::string& key,
                                           const geometry::MeshData& mesh) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (key.empty() || backend_ == nullptr) {
    return kInvalidMesh;
  }

  auto it = runtime_meshes_.find(key);
  if (it != runtime_meshes_.end()) {
    backend_->updateMesh(it->second.mesh, mesh);
    it->second.data = mesh;
    return it->second.mesh;
  }

  const MeshId id = backend_->createMesh(mesh);
  if (id != kInvalidMesh) {
    runtime_meshes_.emplace(key, RuntimeMeshRegistration{.mesh = id, .data = mesh});
  }
  return id;
}

void GraphicsDevice::unregisterRuntimeMesh(const std::string& key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = runtime_meshes_.find(key);
  if (it == runtime_meshes_.end()) {
    return;
  }
  if (backend_ != nullptr && it->second.mesh != kInvalidMesh) {
    backend_->destroyMesh(it->second.mesh);
  }
  runtime_meshes_.erase(it);
}

MeshId GraphicsDevice::findRuntimeMesh(const std::string& key) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  const auto it = runtime_meshes_.find(key);
  return it != runtime_meshes_.end() ? it->second.mesh : kInvalidMesh;
}

MaterialId GraphicsDevice::createMaterial(const ResolvedMaterialDesc& material) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->createMaterial(material) : kInvalidMaterial;
}

MaterialId GraphicsDevice::createMaterial(const MaterialDesc& material) {
  return createMaterial(ResolvedMaterialDesc::fromSurface(material));
}

MaterialId GraphicsDevice::createMaterialFromAsset(const std::filesystem::path& path,
                                                   uint32_t material_index) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->createMaterialFromAsset(path, material_index) : kInvalidMaterial;
}

void GraphicsDevice::updateMaterial(MaterialId material, const MaterialDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->updateMaterial(material, desc);
  }
}

void GraphicsDevice::destroyMaterial(MaterialId material) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->destroyMaterial(material);
  }
}

void GraphicsDevice::setMaterialFloat(MaterialId material, std::string_view name, float value) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setMaterialFloat(material, name, value);
  }
}

TextureId GraphicsDevice::createTexture(const TextureDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->createTexture(desc) : kInvalidTexture;
}

bool GraphicsDevice::supportsTextureFormat(TextureFormat format) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->supportsTextureFormat(format) : false;
}

bool GraphicsDevice::uploadTexture(TextureId texture, const TextureUploadData& upload) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->uploadTexture(texture, upload) : false;
}

void GraphicsDevice::destroyTexture(TextureId texture) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->destroyTexture(texture);
  }
}

RenderTargetId GraphicsDevice::createRenderTarget(const RenderTargetDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->createRenderTarget(desc) : kDefaultRenderTarget;
}

void GraphicsDevice::destroyRenderTarget(RenderTargetId target) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->destroyRenderTarget(target);
  }
}

TerrainId GraphicsDevice::createTerrain(const TerrainDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->createTerrain(desc) : kInvalidTerrain;
}

void GraphicsDevice::destroyTerrain(TerrainId terrain) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->destroyTerrain(terrain);
  }
}

void GraphicsDevice::uploadTerrainTile(TerrainId terrain, const TerrainTileData& tile) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->uploadTerrainTile(terrain, tile);
  }
}

void GraphicsDevice::uploadTerrainMaterialLayer(TerrainId terrain,
                                                const TerrainMaterialLayerData& layer) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->uploadTerrainMaterialLayer(terrain, layer);
  }
}

void GraphicsDevice::clearTerrainMaterialLayers(TerrainId terrain) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->clearTerrainMaterialLayers(terrain);
  }
}

void GraphicsDevice::evictTerrainTile(TerrainId terrain, TerrainTileCoord coord) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->evictTerrainTile(terrain, coord);
  }
}

void GraphicsDevice::submitTerrain(const TerrainDrawItem& item) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->submitTerrain(item);
  }
}

TerrainCapabilities GraphicsDevice::getTerrainCapabilities() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->getTerrainCapabilities() : TerrainCapabilities{};
}

TerrainStats GraphicsDevice::getTerrainStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->getTerrainStats() : TerrainStats{};
}

DeformationId GraphicsDevice::createDeformation(const DeformationDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->createDeformation(desc) : kInvalidDeformation;
}

void GraphicsDevice::updateDeformation(DeformationId deformation,
                                       const DeformationDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->updateDeformation(deformation, desc);
  }
}

void GraphicsDevice::destroyDeformation(DeformationId deformation) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->destroyDeformation(deformation);
  }
}

DeformationStats GraphicsDevice::getDeformationStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->getDeformationStats() : DeformationStats{};
}

void GraphicsDevice::submit(const DrawItem& item) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->submit(item);
  }
}

void GraphicsDevice::submitParticles(ParticleBatch batch) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->submitParticles(std::move(batch));
  }
}

void GraphicsDevice::submitPackedParticles(PackedParticleBatch batch) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->submitPackedParticles(std::move(batch));
  }
}

void GraphicsDevice::submitParticleEmitter(const ParticleEmitterGpuDesc& emitter) {
  if (backend_) {
    backend_->submitParticleEmitter(emitter);
  }
}

void GraphicsDevice::setParticleSystemStats(const ParticlePassStats& stats) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setParticleSystemStats(stats);
  }
}

void GraphicsDevice::retireInstance(InstanceId instance) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->retireInstance(instance);
  }
}

void GraphicsDevice::renderLayer(LayerId layer,
                                 RenderTargetId target,
                                 const PostProcessSettings& post_process) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->renderLayer(layer, target, post_process);
  }
}

void GraphicsDevice::drawLine(const math::Vec3& start, const math::Vec3& end,
                              const math::Color& color, bool depth_test, float thickness) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->drawLine(start, end, color, depth_test, thickness);
  }
}

unsigned int GraphicsDevice::getRenderTargetTextureId(RenderTargetId target) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return backend_ ? backend_->getRenderTargetTextureId(target) : 0u;
}

void GraphicsDevice::setCamera(const CameraData& camera) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setCamera(camera);
  }
}

void GraphicsDevice::setCameraActive(bool active) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setCameraActive(active);
  }
}

void GraphicsDevice::setDirectionalLight(const DirectionalLightData& light) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setDirectionalLight(light);
  }
}

void GraphicsDevice::setLights(const std::vector<LightData>& lights) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setLights(lights);
  }
}

void GraphicsDevice::setEnvironmentMap(const std::filesystem::path& path, float intensity,
                                       bool draw_skybox) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setEnvironmentMap(path, intensity, draw_skybox);
  }
}

void GraphicsDevice::setVsync(bool enabled) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setVsync(enabled);
  }
}

void GraphicsDevice::setAnisotropy(bool enabled, int level) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setAnisotropy(enabled, level);
  }
}

void GraphicsDevice::setGenerateMips(bool enabled) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setGenerateMips(enabled);
  }
}

void GraphicsDevice::setForwardPlusSettings(int tile_size,
                                            int max_lights_per_tile,
                                            int max_local_lights) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setForwardPlusSettings(tile_size, max_lights_per_tile, max_local_lights);
  }
}

ForwardPlusStats GraphicsDevice::getForwardPlusStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!backend_) {
    return {};
  }
  return backend_->getForwardPlusStats();
}

ParticlePassStats GraphicsDevice::getParticlePassStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!backend_) {
    return {};
  }
  return backend_->getParticlePassStats();
}

RendererCommandStats GraphicsDevice::getRendererCommandStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!backend_) {
    return {};
  }
  return backend_->getRendererCommandStats();
}

void GraphicsDevice::setShadowSettings(float bias,
                                       int map_size,
                                       int pcf_radius,
                                       int raster_depth_bias,
                                       float raster_slope_bias,
                                       float receiver_bias_scale,
                                       float normal_bias_scale) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setShadowSettings(bias,
                                map_size,
                                pcf_radius,
                                raster_depth_bias,
                                raster_slope_bias,
                                receiver_bias_scale,
                                normal_bias_scale);
  }
}

void GraphicsDevice::setPointShadowSettings(float constant_bias,
                                            float slope_bias_scale,
                                            float normal_bias_scale,
                                            float receiver_bias_scale) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setPointShadowSettings(constant_bias,
                                     slope_bias_scale,
                                     normal_bias_scale,
                                     receiver_bias_scale);
  }
}

void GraphicsDevice::setPointShadowLightLimit(int max_lights) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setPointShadowLightLimit(max_lights);
  }
}

void GraphicsDevice::setLocalLightingSettings(float distance_damping,
                                              float range_falloff_exponent,
                                              bool ao_affects_local_lights,
                                              float directional_shadow_lift_strength) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setLocalLightingSettings(distance_damping,
                                       range_falloff_exponent,
                                       ao_affects_local_lights,
                                       directional_shadow_lift_strength);
  }
}

void GraphicsDevice::setExposure(float exposure) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->setExposure(exposure);
  }
}

TextureId GraphicsDevice::createTextureRGBA8(int width, int height, const void* pixels) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  renderer::TextureDesc desc{};
  desc.width = width;
  desc.height = height;
  desc.format = renderer::TextureFormat::RGBA8;
  desc.srgb = false;
  desc.generate_mips = false;
  const TextureId id = createTexture(desc);
  if (pixels && id != kInvalidTexture) {
    updateTextureRGBA8(id, width, height, pixels);
  }
  return id;
}

void GraphicsDevice::updateTextureRGBA8(TextureId texture, int width, int height, const void* pixels) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->updateTextureRGBA8(texture, width, height, pixels);
  }
}

void GraphicsDevice::renderUi(const karma::renderer::UIDrawData& draw_data) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (backend_) {
    backend_->renderUi(draw_data);
  }
}

}  // namespace karma::renderer
