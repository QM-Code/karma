#pragma once

#include "karma/renderer/backend.hpp"

namespace karma::renderer {

class GraphicsDevice {
 public:
  explicit GraphicsDevice(karma::platform::Window& window);
  ~GraphicsDevice();

  void beginFrame(const FrameInfo& frame);
  void endFrame();
  void resize(int width, int height);

  MeshId createMesh(const MeshData& mesh);
  MeshId createMeshFromFile(const std::filesystem::path& path);
  void destroyMesh(MeshId mesh);

  MaterialId createMaterial(const MaterialDesc& material);
  void updateMaterial(MaterialId material, const MaterialDesc& desc);
  void destroyMaterial(MaterialId material);
  void setMaterialFloat(MaterialId material, std::string_view name, float value);

  TextureId createTexture(const TextureDesc& desc);
  void destroyTexture(TextureId texture);

  RenderTargetId createRenderTarget(const RenderTargetDesc& desc);
  void destroyRenderTarget(RenderTargetId target);

  void submit(const DrawItem& item);
  void renderLayer(LayerId layer, RenderTargetId target = kDefaultRenderTarget);
  void drawLine(const math::Vec3& start, const math::Vec3& end, const math::Color& color,
                bool depth_test = true, float thickness = 1.0f);

  unsigned int getRenderTargetTextureId(RenderTargetId target) const;

  void setCamera(const CameraData& camera);
  void setCameraActive(bool active);
  void setDirectionalLight(const DirectionalLightData& light);
  void setEnvironmentMap(const std::filesystem::path& path, float intensity);
  void setAnisotropy(bool enabled, int level);
  void setGenerateMips(bool enabled);
  void setShadowSettings(float bias, int map_size, int pcf_radius);
  TextureId createTextureRGBA8(int width, int height, const void* pixels);
  void updateTextureRGBA8(TextureId texture, int width, int height, const void* pixels);
  void renderUi(const karma::app::UIDrawData& draw_data);
  renderer_backend::Backend* backend() { return backend_.get(); }
  const renderer_backend::Backend* backend() const { return backend_.get(); }

 private:
  std::unique_ptr<renderer_backend::Backend> backend_;
};

}  // namespace karma::renderer
