#pragma once

#include "karma/renderer/backend.hpp"

#include <Common/interface/RefCntAutoPtr.hpp>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace karma::platform {
class Window;
}

namespace Diligent {
class IRenderDevice;
class IDeviceContext;
class ISwapChain;
class IBuffer;
class IPipelineState;
class IShaderResourceBinding;
class ITexture;
class ITextureView;
class ISampler;
}  // namespace Diligent

struct aiScene;
struct aiString;

namespace karma::renderer_backend {

class DiligentBackend final : public Backend {
 public:
  explicit DiligentBackend(karma::platform::Window& window);
  ~DiligentBackend() override;

  void beginFrame(const renderer::FrameInfo& frame) override;
  void endFrame() override;
  void resize(int width, int height) override;

  renderer::MeshId createMesh(const renderer::MeshData& mesh) override;
  renderer::MeshId createMeshFromFile(const std::filesystem::path& path) override;
  void destroyMesh(renderer::MeshId mesh) override;

  renderer::MaterialId createMaterial(const renderer::MaterialDesc& material) override;
  void updateMaterial(renderer::MaterialId material, const renderer::MaterialDesc& desc) override;
  void destroyMaterial(renderer::MaterialId material) override;
  void setMaterialFloat(renderer::MaterialId material, std::string_view name, float value) override;

  renderer::TextureId createTexture(const renderer::TextureDesc& desc) override;
  void destroyTexture(renderer::TextureId texture) override;

  renderer::RenderTargetId createRenderTarget(const renderer::RenderTargetDesc& desc) override;
  void destroyRenderTarget(renderer::RenderTargetId target) override;

  void submit(const renderer::DrawItem& item) override;
  void renderLayer(renderer::LayerId layer, renderer::RenderTargetId target) override;
  void drawLine(const math::Vec3& start, const math::Vec3& end,
                const math::Color& color, bool depth_test, float thickness) override;

  unsigned int getRenderTargetTextureId(renderer::RenderTargetId target) const override;

  void setCamera(const renderer::CameraData& camera) override;
  void setCameraActive(bool active) override;
  void setDirectionalLight(const renderer::DirectionalLightData& light) override;
  void setEnvironmentMap(const std::filesystem::path& path, float intensity,
                         bool draw_skybox) override;
  void setAnisotropy(bool enabled, int level) override;
  void setGenerateMips(bool enabled) override;
  void setShadowSettings(float bias, int map_size, int pcf_radius) override;
  void updateTextureRGBA8(renderer::TextureId texture, int w, int h, const void* pixels) override;
  void renderUi(const karma::app::UIDrawData& draw_data) override;

  Diligent::IRenderDevice* getDevice() const { return device_; }
  Diligent::IDeviceContext* getContext() const { return context_; }
  Diligent::ISwapChain* getSwapChain() const { return swap_chain_; }

 private:
  struct MeshRecord {
    renderer::MeshData data;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> vertex_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> index_buffer;
    Diligent::Uint32 vertex_count = 0;
    Diligent::Uint32 index_count = 0;
    glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 bounds_center{0.0f, 0.0f, 0.0f};
    float bounds_radius = 0.0f;
    struct Submesh {
      Diligent::Uint32 index_offset = 0;
      Diligent::Uint32 index_count = 0;
      renderer::MaterialId material = renderer::kInvalidMaterial;
    };
    std::vector<Submesh> submeshes;
  };

  struct MaterialRecord {
    renderer::MaterialDesc desc;
    glm::vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 emissive_factor{0.0f, 0.0f, 0.0f};
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> base_color_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> normal_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> metallic_roughness_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> occlusion_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> emissive_srv;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
  };

  struct TextureRecord {
    renderer::TextureDesc desc;
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> srv;
  };

  struct RenderTargetRecord {
    renderer::RenderTargetDesc desc;
  };

  struct InstanceRecord {
    renderer::LayerId layer = 0;
    renderer::MeshId mesh = renderer::kInvalidMesh;
    renderer::MaterialId material = renderer::kInvalidMaterial;
    glm::mat4 transform{1.0f};
    bool visible = true;
    bool shadow_visible = true;
  };

  struct LineVertex {
    float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  };

  void initializeDevice();
  void clearFrame(const float* color, bool clear_depth);
  void recreateShadowMap();
  void ensureUiResources();
  void ensureLineResources();
  Diligent::RefCntAutoPtr<Diligent::ITextureView> createTextureSRV(const unsigned char* data,
                                                                   int width,
                                                                   int height,
                                                                   bool srgb,
                                                                   bool generate_mips,
                                                                   const char* name,
                                                                   Diligent::RefCntAutoPtr<Diligent::ITexture>& out_texture);
  Diligent::RefCntAutoPtr<Diligent::ITextureView> createSolidTextureSRV(unsigned char r,
                                                                        unsigned char g,
                                                                        unsigned char b,
                                                                        unsigned char a,
                                                                        bool srgb,
                                                                        const char* name,
                                                                        Diligent::RefCntAutoPtr<Diligent::ITexture>& out_texture);
  Diligent::RefCntAutoPtr<Diligent::ITextureView> loadTextureFromAssimp(const aiScene& scene,
                                                                        const std::string& model_key,
                                                                        const std::filesystem::path& base_dir,
                                                                        const aiString& tex_path,
                                                                        bool srgb,
                                                                        const char* label);
  Diligent::RefCntAutoPtr<Diligent::ITextureView> loadTextureFromFile(const std::filesystem::path& path,
                                                                      bool srgb,
                                                                      const char* label);
  void ensureEnvironmentResources();
  void renderSkybox(const glm::mat4& projection, const glm::mat4& view);

  karma::platform::Window* window_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
  Diligent::RefCntAutoPtr<Diligent::ISwapChain> swap_chain_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> shadow_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shader_resources_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shadow_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_color_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_data_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> shadow_sampler_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> shadow_map_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> shadow_map_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> shadow_map_dsv_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> ui_pso_color_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> ui_pso_color_scissor_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> ui_pso_texture_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> ui_pso_texture_scissor_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> ui_srb_color_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> ui_srb_color_scissor_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> ui_srb_texture_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> ui_srb_texture_scissor_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> ui_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> ui_ib_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> ui_cb_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> ui_sampler_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> line_pipeline_state_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> line_pipeline_state_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> line_srb_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> line_srb_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> line_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> line_cb_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_base_color_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_normal_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_metallic_roughness_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_occlusion_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_emissive_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_env_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_base_color_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_normal_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_metallic_roughness_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_occlusion_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_emissive_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_env_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> env_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> env_equirect_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> env_equirect_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> env_cubemap_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> env_cubemap_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> env_irradiance_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> env_irradiance_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> env_prefilter_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> env_prefilter_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> env_brdf_lut_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> env_brdf_lut_srv_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> env_equirect_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> env_equirect_srb_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> env_irradiance_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> env_irradiance_srb_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> env_prefilter_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> env_prefilter_srb_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> brdf_lut_pso_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> skybox_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> skybox_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> env_cube_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> env_cube_ib_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> env_cb_;
  bool env_dirty_ = false;

  renderer::MeshId nextMeshId_ = 1;
  renderer::MaterialId nextMaterialId_ = 1;
  renderer::TextureId nextTextureId_ = 1;
  renderer::RenderTargetId nextTargetId_ = 1;

  std::unordered_map<renderer::MeshId, MeshRecord> meshes_;
  std::unordered_map<renderer::MaterialId, MaterialRecord> materials_;
  std::unordered_map<renderer::TextureId, TextureRecord> textures_;
  std::unordered_map<std::string, renderer::TextureId> texture_cache_;
  std::unordered_map<renderer::RenderTargetId, RenderTargetRecord> targets_;
  std::unordered_map<renderer::InstanceId, InstanceRecord> instances_;
  std::vector<LineVertex> line_vertices_depth_;
  std::vector<LineVertex> line_vertices_no_depth_;

  renderer::CameraData camera_{};
  bool camera_active_ = true;
  float clear_color_[4] = {0.2f, 0.6f, 1.0f, 1.0f};
  renderer::DirectionalLightData directional_light_{};
  std::filesystem::path environment_map_;
  float environment_intensity_ = 0.0f;
  bool draw_skybox_ = true;
  int env_debug_mode_ = 0;
  bool warned_env_debug_ = false;
  bool warned_env_bind_missing_ = false;
  bool anisotropy_enabled_ = false;
  int anisotropy_level_ = 1;
  bool generate_mips_enabled_ = false;
  int shadow_map_size_ = 2048;
  float shadow_bias_ = 0.002f;
  int shadow_pcf_radius_ = 0;
  bool shadow_debug_ = false;
  size_t ui_vb_size_ = 0;
  size_t ui_ib_size_ = 0;
  size_t line_vb_size_ = 0;
  bool warned_line_thickness_ = false;
  int current_width_ = 0;
  int current_height_ = 0;
  bool warned_no_draws_ = false;
};

}  // namespace karma::renderer_backend
