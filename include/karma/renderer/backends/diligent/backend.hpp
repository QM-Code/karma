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
struct aiMaterial;

namespace karma::renderer_backend {

struct DrawConstants;

class DiligentBackend final : public Backend {
 public:
  explicit DiligentBackend(karma::platform::Window& window);
  ~DiligentBackend() override;

  void beginFrame(const renderer::FrameInfo& frame) override;
  void endFrame() override;
  void resize(int width, int height) override;

  renderer::MeshId createMesh(const renderer::MeshData& mesh) override;
  void updateMesh(renderer::MeshId mesh, const renderer::MeshData& data) override;
  renderer::MeshId createMeshFromFile(const std::filesystem::path& path) override;
  void destroyMesh(renderer::MeshId mesh) override;
  bool getMeshBounds(renderer::MeshId mesh, glm::vec3& center, float& radius) const override;

  renderer::MaterialId createMaterial(const renderer::MaterialDesc& material) override;
  renderer::MaterialId createMaterialFromAsset(const std::filesystem::path& path,
                                               uint32_t material_index) override;
  void updateMaterial(renderer::MaterialId material, const renderer::MaterialDesc& desc) override;
  void destroyMaterial(renderer::MaterialId material) override;
  renderer::MaterialSetId createMaterialSetFromMesh(
      renderer::MeshId mesh,
      const renderer::MaterialResourceDesc& desc) override;
  void destroyMaterialSet(renderer::MaterialSetId set) override;
  void setMaterialFloat(renderer::MaterialId material, std::string_view name, float value) override;

  renderer::TextureId createTexture(const renderer::TextureDesc& desc) override;
  void destroyTexture(renderer::TextureId texture) override;

  renderer::RenderTargetId createRenderTarget(const renderer::RenderTargetDesc& desc) override;
  void destroyRenderTarget(renderer::RenderTargetId target) override;

  void submit(const renderer::DrawItem& item) override;
  void submitParticles(renderer::ParticleBatch batch) override;
  void submitPackedParticles(renderer::PackedParticleBatch batch) override;
  void setParticleSystemStats(const renderer::ParticlePassStats& stats) override;
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
  void setVsync(bool enabled) override;
  void setAnisotropy(bool enabled, int level) override;
  void setGenerateMips(bool enabled) override;
  void setForwardPlusSettings(int tile_size,
                              int max_lights_per_tile,
                              int max_local_lights) override;
  renderer::ForwardPlusStats getForwardPlusStats() const override;
  renderer::ParticlePassStats getParticlePassStats() const override;
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
  void setPointShadowLightLimit(int max_lights) override;
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
    renderer::MaterialDesc::ShadingModel shading_model =
        renderer::MaterialDesc::ShadingModel::Standard;
    float shell_fresnel_power = 5.0f;
    float shell_fresnel_strength = 1.0f;
    float shell_refraction_strength = 0.08f;
    float shell_interior_strength = 0.4f;
    float shell_highlight_strength = 1.0f;
    float shell_alpha_boost = 0.0f;
    float shell_swirl_strength = 0.0f;
    bool analytic_sphere_normals = false;
    float shell_body_strength = 1.0f;
    float screen_center_x = 0.5f;
    float screen_center_y = 0.5f;
    float screen_radius_x = 0.25f;
    float screen_radius_y = 0.25f;
    float wave_tint_strength = 0.75f;
    float wave_distortion_strength = 0.6f;
    float wave_edge_strength = 0.35f;
    float wave_noise_strength = 0.65f;
    glm::vec3 volume_center{0.0f, 0.0f, 0.0f};
    float volume_radius = 1.0f;
    float volume_density = 1.0f;
    renderer::MaterialDesc::BlendMode blend_mode = renderer::MaterialDesc::BlendMode::Alpha;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> base_color_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> normal_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> metallic_roughness_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> occlusion_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> emissive_srv;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> transparent_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> transparent_double_sided_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> additive_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> additive_double_sided_srb;
  };

  struct MaterialSetRecord {
    renderer::MeshId source_mesh = renderer::kInvalidMesh;
    std::vector<renderer::MaterialId> materials;
  };

  struct ImportedMaterialTemplateCacheEntry {
    std::vector<MaterialRecord> materials;
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
    Diligent::RefCntAutoPtr<Diligent::ITextureView> depth_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> depth_dsv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> depth_read_only_dsv;
  };

  struct InstanceRecord {
    renderer::LayerId layer = 0;
    renderer::MeshId mesh = renderer::kInvalidMesh;
    renderer::MaterialId material = renderer::kInvalidMaterial;
    renderer::MaterialSetId material_set = renderer::kInvalidMaterialSet;
    glm::mat4 transform{1.0f};
    bool visible = true;
    bool shadow_visible = true;
    bool transform_changed = true;
  };

  struct ForwardBatchKey {
    renderer::MeshId mesh = renderer::kInvalidMesh;
    renderer::MaterialId material = renderer::kInvalidMaterial;
    Diligent::Uint32 index_offset = 0;
    Diligent::Uint32 index_count = 0;
    bool indexed = false;

    bool operator==(const ForwardBatchKey& other) const {
      return mesh == other.mesh &&
             material == other.material &&
             index_offset == other.index_offset &&
             index_count == other.index_count &&
             indexed == other.indexed;
    }
  };

  struct ForwardBatch {
    ForwardBatchKey key{};
    std::vector<glm::mat4> transforms;
  };

  struct TransparentForwardDraw {
    ForwardBatchKey key{};
    glm::mat4 transform{1.0f};
    float depth = 0.0f;
  };

  struct ForwardLayerState {
    std::vector<ForwardBatch> opaque_batches;
    std::vector<TransparentForwardDraw> transparent_draws;
    std::vector<TransparentForwardDraw> pre_particle_scene_sample_draws;
    std::vector<TransparentForwardDraw> post_particle_draws;
  };

  struct ForwardLayerStats {
    Diligent::Uint32 skipped_hidden = 0;
    Diligent::Uint32 skipped_missing_vb = 0;
    Diligent::Uint32 skipped_missing_mesh = 0;
    Diligent::Uint32 skipped_layer = 0;
  };

  struct ParticlePassContext {
    glm::mat4 view_proj{1.0f};
    glm::vec3 camera_forward{0.0f, 0.0f, -1.0f};
    glm::vec3 camera_up{0.0f, 1.0f, 0.0f};
    glm::vec3 camera_right{1.0f, 0.0f, 0.0f};
    Diligent::ITextureView* active_rtv = nullptr;
    Diligent::ITextureView* active_dsv = nullptr;
    Diligent::ITextureView* particle_dsv = nullptr;
    Diligent::ITextureView* particle_scene_color_sample_srv = nullptr;
    Diligent::ITextureView* particle_scene_depth_srv = nullptr;
    int render_width = 0;
    int render_height = 0;
    Diligent::TEXTURE_FORMAT scene_color_format = Diligent::TEX_FORMAT_UNKNOWN;
    bool allow_distortion_particles = false;
  };

  struct LineVertex {
    float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  };

  struct ParticleBatchRecord {
    renderer::LayerId layer = 0;
    bool depth_test = true;
    renderer::TextureId texture = renderer::kInvalidTexture;
    renderer::ParticleBlendMode blend_mode = renderer::ParticleBlendMode::Additive;
    renderer::ParticleAlignment alignment = renderer::ParticleAlignment::Billboard;
    renderer::ParticleShadingMode shading_mode = renderer::ParticleShadingMode::Standard;
    renderer::ParticlePresentationMode presentation_mode =
        renderer::ParticlePresentationMode::Baked;
    bool use_soft_mask = true;
    float soft_particle_distance = 0.0f;
    float distortion_strength = 0.0f;
    float fresnel_power = 4.0f;
    float fresnel_strength = 1.0f;
    float refraction_strength = 0.0f;
    float interior_glow = 0.0f;
    float size_curve_exponent = 1.0f;
    float alpha_curve_exponent = 1.0f;
    uint32_t atlas_columns = 1u;
    uint32_t atlas_rows = 1u;
    uint32_t atlas_frame_count = 1u;
    bool animate_over_lifetime = false;
    uint32_t atlas_frame_width = 0u;
    uint32_t atlas_frame_height = 0u;
    uint32_t atlas_border_x = 0u;
    uint32_t atlas_border_y = 0u;
    uint32_t atlas_spacing_x = 0u;
    uint32_t atlas_spacing_y = 0u;
    float animation_fps = 0.0f;
    std::vector<renderer::ParticlePackedInstance> particles;
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
  void ensureParticleResources();
  void ensureDefaultSceneResources(int width, int height);
  void ensureParticleSceneCopyResources(int width,
                                        int height,
                                        Diligent::TEXTURE_FORMAT format);
  void ensureParticleHalfResAlphaResources(int width,
                                           int height,
                                           Diligent::TEXTURE_FORMAT format);
  void ensureParticleFallbackDepthResource();
  void uploadMeshBuffers(const renderer::MeshData& mesh, MeshRecord& record);
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
  MaterialRecord buildImportedMaterialRecord(const aiScene& scene,
                                             const aiMaterial& material,
                                             const std::filesystem::path& asset_path);
  void initializeMaterialBindings(MaterialRecord& record);
  void bindShadowResourcesToSrb(Diligent::IShaderResourceBinding* srb) const;
  const ImportedMaterialTemplateCacheEntry* getImportedMaterialTemplates(
      const std::filesystem::path& path);
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
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> transparent_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> transparent_double_sided_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> additive_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> additive_double_sided_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> camera_override_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> camera_override_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> camera_override_user_constants_;
  std::filesystem::path camera_override_vertex_path_;
  std::filesystem::path camera_override_fragment_path_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> shadow_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shader_resources_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> transparent_default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>
      transparent_double_sided_default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> additive_default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>
      additive_double_sided_default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shadow_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_color_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_data_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> shadow_sampler_;
  static constexpr int kShadowCascadeCount = 4;
  static constexpr int kMaxPointShadowLights = 16;
  static constexpr int kPointShadowFaceCount = 6;
  static constexpr int kPointShadowMatrixCount =
      kMaxPointShadowLights * kPointShadowFaceCount;
  struct ShadowLayerState {
    std::array<glm::mat4, kShadowCascadeCount> cascade_light_view_proj{};
    std::array<glm::mat4, kShadowCascadeCount> cascade_shadow_uv_proj{};
    std::array<float, kShadowCascadeCount> cascade_world_texel{};
    std::array<float, kShadowCascadeCount> cascade_splits{};
    std::array<glm::mat4, kPointShadowMatrixCount> point_shadow_uv_proj{};
    std::array<renderer::LightData, kMaxPointShadowLights> point_shadow_lights{};
    std::array<size_t, kMaxPointShadowLights> point_shadow_light_source_indices{};
    std::array<size_t, kMaxPointShadowLights> point_shadow_local_light_indices{};
    Diligent::Uint32 point_shadow_light_count = 0;
    bool point_shadow_ready = false;
  };
  void renderShadowLayer(renderer::LayerId layer,
                         float aspect,
                         const glm::mat4& depth_fix,
                         const glm::vec3& camera_position,
                         const glm::vec3& cam_forward,
                         const glm::vec3& cam_up,
                         const glm::vec3& cam_right,
                         bool is_gl,
                         float fixed_bias,
                         float shadow_texel_param,
                         float point_shadow_texel_size,
                         const std::vector<size_t>& local_light_source_indices,
                         ShadowLayerState& out_state);
  void collectForwardLayerState(renderer::LayerId layer,
                                const glm::mat4& view_proj,
                                const glm::vec3& camera_position,
                                const glm::vec3& camera_forward,
                                bool is_gl,
                                ForwardLayerState& out_state,
                                ForwardLayerStats& out_stats) const;
  Diligent::Uint32 renderOpaqueForwardLayer(const ForwardLayerState& state,
                                            const DrawConstants& base_constants,
                                            Diligent::IPipelineState* active_forward_pipeline,
                                            bool use_custom_shader_override,
                                            Diligent::ITextureView* active_rtv,
                                            Diligent::ITextureView* active_dsv,
                                            int render_width,
                                            int render_height);
  Diligent::Uint32 renderTransparentForwardDraws(
      const std::vector<TransparentForwardDraw>& draws,
      const DrawConstants& base_constants,
      Diligent::IPipelineState* active_forward_pipeline,
      bool use_custom_shader_override,
      Diligent::ITextureView* active_rtv,
      Diligent::ITextureView* active_dsv,
      Diligent::ITextureView* particle_dsv,
      int render_width,
      int render_height,
      Diligent::ITextureView* scene_color_sample_srv,
      Diligent::ITextureView* scene_depth_sample_srv);
  bool forwardDrawsRequireSceneColorCopy(
      const std::vector<TransparentForwardDraw>& draws) const;
  void renderParticlePasses(renderer::LayerId layer, const ParticlePassContext& context);
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
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_additive_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_additive_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_alpha_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_alpha_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_alpha_half_res_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_distortion_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_distortion_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_half_res_composite_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_srb_additive_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_srb_additive_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_srb_alpha_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_srb_alpha_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_srb_alpha_half_res_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_srb_distortion_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_srb_distortion_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_half_res_composite_srb_;
  Diligent::IShaderResourceVariable* particle_texture_var_additive_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_texture_var_additive_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_texture_var_alpha_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_texture_var_alpha_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_texture_var_alpha_half_res_ = nullptr;
  Diligent::IShaderResourceVariable* particle_texture_var_distortion_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_texture_var_distortion_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_color_var_additive_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_color_var_additive_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_color_var_alpha_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_color_var_alpha_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_color_var_alpha_half_res_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_color_var_distortion_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_color_var_distortion_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_depth_var_additive_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_depth_var_additive_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_depth_var_alpha_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_depth_var_alpha_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_depth_var_alpha_half_res_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_depth_var_distortion_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_scene_depth_var_distortion_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_half_res_alpha_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_instance_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_cb_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_scene_color_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_scene_color_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_scene_color_rtv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_scene_depth_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_scene_depth_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_scene_depth_dsv_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_scene_depth_read_only_dsv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> particle_scene_color_copy_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> particle_scene_color_copy_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> particle_half_res_alpha_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> particle_half_res_alpha_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> particle_half_res_alpha_rtv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> particle_fallback_depth_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> particle_fallback_depth_srv_;
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
  renderer::MaterialSetId nextMaterialSetId_ = 1;
  renderer::TextureId nextTextureId_ = 1;
  renderer::RenderTargetId nextTargetId_ = 1;

  std::unordered_map<renderer::MeshId, MeshRecord> meshes_;
  std::unordered_map<renderer::MaterialId, MaterialRecord> materials_;
  std::unordered_map<renderer::MaterialSetId, MaterialSetRecord> material_sets_;
  std::unordered_map<std::string, ImportedMaterialTemplateCacheEntry> imported_material_templates_;
  std::unordered_map<renderer::TextureId, TextureRecord> textures_;
  std::unordered_map<std::string, renderer::TextureId> texture_cache_;
  std::unordered_map<renderer::RenderTargetId, RenderTargetRecord> targets_;
  std::unordered_map<renderer::InstanceId, InstanceRecord> instances_;
  std::vector<ParticleBatchRecord> particle_batches_;
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
  bool vsync_enabled_ = true;
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
  int point_shadow_max_lights_ = 2;
  size_t ui_vb_size_ = 0;
  size_t ui_ib_size_ = 0;
  size_t instance_vb_capacity_ = 0;
  size_t forward_plus_light_capacity_ = 0;
  size_t forward_plus_tile_count_capacity_ = 0;
  size_t forward_plus_tile_index_capacity_ = 0;
  size_t line_vb_size_ = 0;
  size_t particle_instance_capacity_ = 0;
  int default_scene_width_ = 0;
  int default_scene_height_ = 0;
  int particle_scene_color_copy_width_ = 0;
  int particle_scene_color_copy_height_ = 0;
  Diligent::TEXTURE_FORMAT particle_scene_color_copy_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  int particle_half_res_alpha_width_ = 0;
  int particle_half_res_alpha_height_ = 0;
  Diligent::TEXTURE_FORMAT particle_half_res_alpha_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  int forward_plus_tile_size_ = 16;
  int forward_plus_max_lights_per_tile_ = 128;
  renderer::ForwardPlusStats forward_plus_stats_{};
  renderer::ParticlePassStats particle_pass_stats_{};
  renderer::ParticlePassStats particle_stats_log_totals_{};
  double particle_stats_log_elapsed_seconds_ = 0.0;
  float last_frame_delta_seconds_ = 0.0f;
  uint32_t particle_stats_log_frame_count_ = 0;
  bool particle_stats_log_initialized_ = false;
  bool particle_stats_log_enabled_ = false;
  bool warned_line_thickness_ = false;
  int current_width_ = 0;
  int current_height_ = 0;
  bool warned_no_draws_ = false;
  static constexpr renderer::TextureId kRenderTargetTextureHandleBit = 0x80000000u;
  int forward_plus_max_local_lights_ = 4096;

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
  double accumulated_time_seconds_ = 0.0;
  // Keep the cache responsive enough for animated point lights in the probe.
  float point_shadow_position_threshold_ = 0.001f;
  float point_shadow_range_threshold_ = 0.05f;
};

}  // namespace karma::renderer_backend
