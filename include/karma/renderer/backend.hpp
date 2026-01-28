#pragma once

#include "karma/renderer/types.h"
#include "karma/platform/events.h"

#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace karma::platform {
class Window;
}

namespace karma::renderer_backend {

class Backend {
 public:
  virtual ~Backend() = default;

  virtual void beginFrame(const renderer::FrameInfo& frame) = 0;
  virtual void endFrame() = 0;
  virtual void resize(int width, int height) = 0;

  virtual renderer::MeshId createMesh(const renderer::MeshData& mesh) = 0;
  virtual renderer::MeshId createMeshFromFile(const std::filesystem::path& path) = 0;
  virtual void destroyMesh(renderer::MeshId mesh) = 0;

  virtual renderer::MaterialId createMaterial(const renderer::MaterialDesc& material) = 0;
  virtual void updateMaterial(renderer::MaterialId material, const renderer::MaterialDesc& desc) = 0;
  virtual void destroyMaterial(renderer::MaterialId material) = 0;
  virtual void setMaterialFloat(renderer::MaterialId material, std::string_view name, float value) = 0;

  virtual renderer::TextureId createTexture(const renderer::TextureDesc& desc) = 0;
  virtual void destroyTexture(renderer::TextureId texture) = 0;

  virtual renderer::RenderTargetId createRenderTarget(const renderer::RenderTargetDesc& desc) = 0;
  virtual void destroyRenderTarget(renderer::RenderTargetId target) = 0;

  virtual void submit(const renderer::DrawItem& item) = 0;
  virtual void renderLayer(renderer::LayerId layer, renderer::RenderTargetId target) = 0;

  virtual unsigned int getRenderTargetTextureId(renderer::RenderTargetId target) const = 0;

  virtual void setCamera(const renderer::CameraData& camera) = 0;
  virtual void setCameraActive(bool active) = 0;
  virtual void setDirectionalLight(const renderer::DirectionalLightData& light) = 0;
  virtual void setEnvironmentMap(const std::filesystem::path& path, float intensity) = 0;
  virtual void setAnisotropy(bool enabled, int level) = 0;
  virtual void setGenerateMips(bool enabled) = 0;
  virtual void setShadowSettings(float bias, int map_size, int pcf_radius) = 0;

  virtual void setOverlayCallback(std::function<void()> callback) = 0;
  virtual void handleOverlayEvent(const platform::Event& event) = 0;
  virtual void renderOverlay() = 0;
};

std::unique_ptr<Backend> CreateGraphicsBackend(karma::platform::Window& window);

}  // namespace karma::renderer_backend
