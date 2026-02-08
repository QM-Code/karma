#include "karma/renderer/device.h"


namespace karma::renderer {

GraphicsDevice::GraphicsDevice(karma::platform::Window& window) {
  backend_ = renderer_backend::CreateGraphicsBackend(window);
}

GraphicsDevice::~GraphicsDevice() = default;

void GraphicsDevice::beginFrame(const FrameInfo& frame) {
  if (backend_) {
    backend_->beginFrame(frame);
  }
}

void GraphicsDevice::endFrame() {
  if (backend_) {
    backend_->endFrame();
  }
}

void GraphicsDevice::resize(int width, int height) {
  if (backend_) {
    backend_->resize(width, height);
  }
}

MeshId GraphicsDevice::createMesh(const MeshData& mesh) {
  return backend_ ? backend_->createMesh(mesh) : kInvalidMesh;
}

MeshId GraphicsDevice::createMeshFromFile(const std::filesystem::path& path) {
  return backend_ ? backend_->createMeshFromFile(path) : kInvalidMesh;
}

void GraphicsDevice::destroyMesh(MeshId mesh) {
  if (backend_) {
    backend_->destroyMesh(mesh);
  }
}

bool GraphicsDevice::getMeshBounds(MeshId mesh, glm::vec3& center, float& radius) const {
  if (!backend_) {
    return false;
  }
  return backend_->getMeshBounds(mesh, center, radius);
}

MaterialId GraphicsDevice::createMaterial(const MaterialDesc& material) {
  return backend_ ? backend_->createMaterial(material) : kInvalidMaterial;
}

void GraphicsDevice::updateMaterial(MaterialId material, const MaterialDesc& desc) {
  if (backend_) {
    backend_->updateMaterial(material, desc);
  }
}

void GraphicsDevice::destroyMaterial(MaterialId material) {
  if (backend_) {
    backend_->destroyMaterial(material);
  }
}

void GraphicsDevice::setMaterialFloat(MaterialId material, std::string_view name, float value) {
  if (backend_) {
    backend_->setMaterialFloat(material, name, value);
  }
}

TextureId GraphicsDevice::createTexture(const TextureDesc& desc) {
  return backend_ ? backend_->createTexture(desc) : kInvalidTexture;
}

void GraphicsDevice::destroyTexture(TextureId texture) {
  if (backend_) {
    backend_->destroyTexture(texture);
  }
}

RenderTargetId GraphicsDevice::createRenderTarget(const RenderTargetDesc& desc) {
  return backend_ ? backend_->createRenderTarget(desc) : kDefaultRenderTarget;
}

void GraphicsDevice::destroyRenderTarget(RenderTargetId target) {
  if (backend_) {
    backend_->destroyRenderTarget(target);
  }
}

void GraphicsDevice::submit(const DrawItem& item) {
  if (backend_) {
    backend_->submit(item);
  }
}

void GraphicsDevice::retireInstance(InstanceId instance) {
  if (backend_) {
    backend_->retireInstance(instance);
  }
}

void GraphicsDevice::renderLayer(LayerId layer, RenderTargetId target) {
  if (backend_) {
    backend_->renderLayer(layer, target);
  }
}

void GraphicsDevice::drawLine(const math::Vec3& start, const math::Vec3& end,
                              const math::Color& color, bool depth_test, float thickness) {
  if (backend_) {
    backend_->drawLine(start, end, color, depth_test, thickness);
  }
}

unsigned int GraphicsDevice::getRenderTargetTextureId(RenderTargetId target) const {
  return backend_ ? backend_->getRenderTargetTextureId(target) : 0u;
}

void GraphicsDevice::setCamera(const CameraData& camera) {
  if (backend_) {
    backend_->setCamera(camera);
  }
}

void GraphicsDevice::setCameraActive(bool active) {
  if (backend_) {
    backend_->setCameraActive(active);
  }
}

void GraphicsDevice::setDirectionalLight(const DirectionalLightData& light) {
  if (backend_) {
    backend_->setDirectionalLight(light);
  }
}

void GraphicsDevice::setLights(const std::vector<LightData>& lights) {
  if (backend_) {
    backend_->setLights(lights);
  }
}

void GraphicsDevice::setEnvironmentMap(const std::filesystem::path& path, float intensity,
                                       bool draw_skybox) {
  if (backend_) {
    backend_->setEnvironmentMap(path, intensity, draw_skybox);
  }
}

void GraphicsDevice::setAnisotropy(bool enabled, int level) {
  if (backend_) {
    backend_->setAnisotropy(enabled, level);
  }
}

void GraphicsDevice::setGenerateMips(bool enabled) {
  if (backend_) {
    backend_->setGenerateMips(enabled);
  }
}

void GraphicsDevice::setShadowSettings(float bias,
                                       int map_size,
                                       int pcf_radius,
                                       int raster_depth_bias,
                                       float raster_slope_bias,
                                       float receiver_bias_scale,
                                       float normal_bias_scale) {
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

TextureId GraphicsDevice::createTextureRGBA8(int width, int height, const void* pixels) {
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
  if (backend_) {
    backend_->updateTextureRGBA8(texture, width, height, pixels);
  }
}

void GraphicsDevice::renderUi(const karma::app::UIDrawData& draw_data) {
  if (backend_) {
    backend_->renderUi(draw_data);
  }
}

}  // namespace karma::renderer
