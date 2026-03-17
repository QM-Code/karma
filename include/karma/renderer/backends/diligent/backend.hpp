#pragma once

#include "karma/renderer/backend.hpp"

#include <Common/interface/RefCntAutoPtr.hpp>
#include <Graphics/GraphicsTools/interface/RenderStateCache.hpp>
#include <filesystem>
#include <cstdint>
#include <array>
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
class IBufferView;
class IPipelineState;
class IShaderResourceBinding;
class ITexture;
class ITextureView;
class ISampler;
class IShaderResourceVariable;
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
  bool getMeshBounds(renderer::MeshId mesh, glm::vec3& center, float& radius) const override;

  renderer::MaterialId createMaterial(const renderer::MaterialDesc& material) override;
  void updateMaterial(renderer::MaterialId material, const renderer::MaterialDesc& desc) override;
  void destroyMaterial(renderer::MaterialId material) override;
  void setMaterialFloat(renderer::MaterialId material, std::string_view name, float value) override;

  renderer::TextureId createTexture(const renderer::TextureDesc& desc) override;
  void destroyTexture(renderer::TextureId texture) override;

  renderer::RenderTargetId createRenderTarget(const renderer::RenderTargetDesc& desc) override;
  void destroyRenderTarget(renderer::RenderTargetId target) override;

  void submit(const renderer::DrawItem& item) override;
  void retireInstance(renderer::InstanceId instance) override;
  void renderLayer(renderer::LayerId layer, renderer::RenderTargetId target) override;
  void drawLine(const math::Vec3& start, const math::Vec3& end,
                const math::Color& color, bool depth_test, float thickness) override;

  unsigned int getRenderTargetTextureId(renderer::RenderTargetId target) const override;

  void setCamera(const renderer::CameraData& camera) override;
  void setCameraActive(bool active) override;
  void setDirectionalLight(const renderer::DirectionalLightData& light) override;
  void setLights(const std::vector<renderer::LightData>& lights) override;
  void setEnvironmentMap(const std::filesystem::path& path, float intensity,
                         bool draw_skybox) override;
  void setAnisotropy(bool enabled, int level) override;
  void setGenerateMips(bool enabled) override;
  void setForwardPlusSettings(int tile_size, int max_lights_per_tile) override;
  renderer::ForwardPlusStats getForwardPlusStats() const override;
  void setShadowSettings(float bias,
                         int map_size,
                         int pcf_radius,
                         int raster_depth_bias,
                         float raster_slope_bias,
                         float receiver_bias_scale,
                         float normal_bias_scale) override;
  void setPointShadowSettings(float constant_bias,
                              float slope_bias_scale,
                              float normal_bias_scale,
                              float receiver_bias_scale) override;
  void setLocalLightingSettings(float distance_damping,
                                float range_falloff_exponent,
                                bool ao_affects_local_lights,
                                float directional_shadow_lift_strength) override;
  void setExposure(float exposure) override;
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
    std::vector<renderer::MaterialId> owned_materials;
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
    int width = 0;
    int height = 0;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> color_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> color_rtv;
    Diligent::RefCntAutoPtr<Diligent::ITexture> depth_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> depth_dsv;
  };

  struct InstanceRecord {
    renderer::LayerId layer = 0;
    renderer::MeshId mesh = renderer::kInvalidMesh;
    renderer::MaterialId material = renderer::kInvalidMaterial;
    glm::mat4 transform{1.0f};
    bool visible = true;
    bool shadow_visible = true;
    bool transform_changed = true;
  };

  struct LineVertex {
    float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  };

  void initializeDevice();
  void clearFrame(const float* color, bool clear_depth);
  void recreateShadowMap();
  void recreatePointShadowMap();
  void recreateRenderTargetResources(RenderTargetRecord& record, int width, int height);
  void recreateShadowPipeline();
  bool ensureCameraOverridePipeline(const renderer::CameraData& camera);
  void updateCameraOverrideUserConstants(const renderer::CameraData& camera);
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
  Diligent::RenderDeviceWithCache<false> device_with_cache_;
  std::filesystem::path render_state_cache_path_;
  bool shader_cache_enabled_ = true;
  bool shader_cache_log_ = false;
  std::uint32_t shader_cache_version_ = 3;
  bool shader_cache_flush_ = false;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> depth_prepass_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> camera_override_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> camera_override_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> camera_override_user_constants_;
  std::filesystem::path camera_override_vertex_path_;
  std::filesystem::path camera_override_fragment_path_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> shadow_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shader_resources_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shadow_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_color_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_data_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> shadow_sampler_;
  static constexpr int kShadowCascadeCount = 4;
  static constexpr int kMaxPointShadowLights = 2;
  static constexpr int kPointShadowFaceCount = 6;
  static constexpr int kPointShadowMatrixCount =
      kMaxPointShadowLights * kPointShadowFaceCount;
  Diligent::RefCntAutoPtr<Diligent::ITexture> shadow_map_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> shadow_map_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> shadow_map_dsv_;
  std::array<Diligent::RefCntAutoPtr<Diligent::ITextureView>, kShadowCascadeCount>
      shadow_map_dsv_cascades_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> point_shadow_map_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> point_shadow_map_srv_;
  std::array<Diligent::RefCntAutoPtr<Diligent::ITextureView>, kPointShadowMatrixCount>
      point_shadow_map_dsv_faces_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> ui_pso_color_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> ui_pso_color_scissor_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> ui_pso_texture_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> ui_pso_texture_scissor_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> ui_srb_color_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> ui_srb_color_scissor_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> ui_srb_texture_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> ui_srb_texture_scissor_;
  Diligent::IShaderResourceVariable* ui_texture_var_ = nullptr;
  Diligent::IShaderResourceVariable* ui_texture_scissor_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> ui_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> ui_ib_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> ui_cb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> instance_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> forward_plus_light_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> forward_plus_tile_count_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> forward_plus_tile_index_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> forward_plus_light_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> forward_plus_tile_count_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> forward_plus_tile_index_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> forward_plus_tile_count_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> forward_plus_tile_index_uav_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> forward_plus_compute_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> forward_plus_compute_srb_;
  Diligent::IShaderResourceVariable* forward_plus_compute_lights_var_ = nullptr;
  Diligent::IShaderResourceVariable* forward_plus_compute_tile_counts_var_ = nullptr;
  Diligent::IShaderResourceVariable* forward_plus_compute_tile_indices_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> forward_plus_compute_cb_;
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
  Diligent::IShaderResourceVariable* skybox_texture_var_ = nullptr;
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
  std::vector<renderer::LightData> lights_;
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
  float shadow_bias_ = 0.0006f;
  int shadow_pcf_radius_ = 0;
  int shadow_raster_depth_bias_ = 0;
  float shadow_raster_slope_bias_ = 0.0f;
  float shadow_receiver_bias_scale_ = 0.75f;
  float shadow_normal_bias_scale_ = 1.0f;
  float shadow_split_lambda_ = 0.7f;
  float point_shadow_constant_bias_ = 0.0012f;
  float point_shadow_slope_bias_scale_ = 2.0f;
  float point_shadow_normal_bias_scale_ = 1.5f;
  float point_shadow_receiver_bias_scale_ = 0.35f;
  float local_light_distance_damping_ = 0.02f;
  float local_light_range_exponent_ = 1.1f;
  bool ao_affects_local_lights_ = false;
  float local_light_directional_shadow_lift_ = 0.0f;
  float lighting_exposure_ = 1.0f;
  int point_shadow_map_size_ = 1024;
  size_t ui_vb_size_ = 0;
  size_t ui_ib_size_ = 0;
  size_t instance_vb_capacity_ = 0;
  size_t forward_plus_light_capacity_ = 0;
  size_t forward_plus_tile_count_capacity_ = 0;
  size_t forward_plus_tile_index_capacity_ = 0;
  size_t line_vb_size_ = 0;
  int forward_plus_tile_size_ = 16;
  int forward_plus_max_lights_per_tile_ = 128;
  renderer::ForwardPlusStats forward_plus_stats_{};
  bool warned_line_thickness_ = false;
  int current_width_ = 0;
  int current_height_ = 0;
  bool warned_no_draws_ = false;
  static constexpr renderer::TextureId kRenderTargetTextureHandleBit = 0x80000000u;

  bool directional_shadow_cache_valid_ = false;
  std::array<glm::mat4, kShadowCascadeCount> cached_cascade_light_view_proj_{};
  std::array<glm::mat4, kShadowCascadeCount> cached_cascade_shadow_uv_proj_{};
  std::array<float, kShadowCascadeCount> cached_cascade_world_texel_{};
  std::array<float, kShadowCascadeCount> cached_cascade_splits_{};
  glm::vec3 cached_shadow_camera_position_{0.0f, 0.0f, 0.0f};
  glm::vec3 cached_shadow_camera_forward_{0.0f, 0.0f, -1.0f};
  glm::vec3 cached_shadow_light_direction_{0.0f, -1.0f, 0.0f};
  float cached_shadow_camera_aspect_ = 1.0f;
  float cached_shadow_camera_fov_y_degrees_ = 60.0f;
  float cached_shadow_camera_near_ = 0.1f;
  float cached_shadow_camera_far_ = 1000.0f;
  bool cached_shadow_camera_perspective_ = true;
  float directional_shadow_position_threshold_ = 0.12f;
  float directional_shadow_angle_threshold_deg_ = 0.3f;

  bool point_shadow_cache_initialized_ = false;
  std::array<glm::mat4, kPointShadowMatrixCount> cached_point_shadow_uv_proj_{};
  std::array<int32_t, kMaxPointShadowLights> point_shadow_slot_source_index_{};
  std::array<glm::vec3, kMaxPointShadowLights> point_shadow_slot_position_{};
  std::array<float, kMaxPointShadowLights> point_shadow_slot_range_{};
  std::array<bool, kMaxPointShadowLights> point_shadow_slot_valid_{};
  std::array<uint8_t, kPointShadowMatrixCount> point_shadow_face_dirty_{};
  Diligent::Uint32 point_shadow_face_cursor_ = 0;
  Diligent::Uint32 point_shadow_faces_per_frame_budget_ = 2;
  float point_shadow_position_threshold_ = 0.05f;
  float point_shadow_range_threshold_ = 0.05f;
};

}  // namespace karma::renderer_backend
