#pragma once

#include "karma/core.h"
#include "private/rendering/backend.hpp"

#include "passes/pass_shared.h"

#include <Common/interface/RefCntAutoPtr.hpp>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/PipelineStateCache.h>
#include <Graphics/GraphicsTools/interface/RenderStateCache.hpp>
#include <filesystem>
#include <cstdint>
#include <cstddef>
#include <array>
#include <limits>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <string_view>
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
class IPipelineStateCache;
class IShaderResourceBinding;
class ITexture;
class ITextureView;
class ISampler;
class IShader;
class IShaderResourceVariable;
}  // namespace Diligent

struct aiScene;
struct aiString;
struct aiMaterial;

namespace karma::rendering::backend {

struct DrawConstants;

struct PostProcessTexture {
  Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> srv;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> rtv;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> dsv;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> read_only_dsv;
  int width = 0;
  int height = 0;
  Diligent::TEXTURE_FORMAT format = Diligent::TEX_FORMAT_UNKNOWN;
  uint32_t sample_count = 1u;
};

struct PostProcessPassResources {
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
  Diligent::IShaderResourceVariable* source_var = nullptr;
  Diligent::IShaderResourceVariable* depth_var = nullptr;
  Diligent::IShaderResourceVariable* bloom_var = nullptr;
  Diligent::IShaderResourceVariable* history_var = nullptr;
  Diligent::IShaderResourceVariable* sampler_var = nullptr;
};

struct FrameGraphShaderPassResources {
  std::string cache_key;
  Diligent::TEXTURE_FORMAT rtv_format = Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
  std::unordered_map<std::string, Diligent::IShaderResourceVariable*> texture_vars;
  Diligent::IShaderResourceVariable* sampler_var = nullptr;
};

struct FrameGraphSceneMaskResources {
  std::string cache_key;
  Diligent::TEXTURE_FORMAT rtv_format = Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::TEXTURE_FORMAT dsv_format = Diligent::TEX_FORMAT_UNKNOWN;
  rendering::InstanceGpuLayout layout = rendering::InstanceGpuLayout::Matrix4x4Params;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
};

enum class MaterialPipelineKind : uint32_t {
  Standard = 0,
  EnergyShell = 1,
  WaveVolume = 2,
  SphereHalo = 3,
  ScreenWave = 4,
  SphereGlowVolume = 5,
  VolumetricSolid = 6,
  Foliage = 7,
};

class DiligentBackend final : public Backend {
 public:
	  explicit DiligentBackend(karma::platform::Window& window,
	                           const rendering::GraphicsDeviceCreateInfo& create_info = {});
  ~DiligentBackend() override;

  void beginFrame(const rendering::FrameInfo& frame) override;
  void endFrame() override;
  void resize(int width, int height) override;
  void prewarmRendererResources(bool include_ui) override;
  void flushRenderStateCache() override;

  rendering::MeshId createMesh(const world::MeshData& mesh) override;
  void updateMesh(rendering::MeshId mesh, const world::MeshData& data) override;
  void destroyMesh(rendering::MeshId mesh) override;
  bool getMeshBounds(rendering::MeshId mesh, glm::vec3& center, float& radius) const override;
  bool getMeshMaterialSlots(rendering::MeshId mesh,
                            std::vector<world::MeshMaterialSlot>& out_slots) const override;

  rendering::MaterialId createMaterial(const rendering::ResolvedMaterialDesc& material) override;
  rendering::MaterialId createMaterialFromAsset(const std::filesystem::path& path,
                                               uint32_t material_index) override;
  void updateMaterial(rendering::MaterialId material, const rendering::MaterialDesc& desc) override;
  void destroyMaterial(rendering::MaterialId material) override;
  void setMaterialFloat(rendering::MaterialId material, std::string_view name, float value) override;

  rendering::TextureId createTexture(const rendering::TextureDesc& desc) override;
  bool supportsTextureFormat(rendering::TextureFormat format) const override;
  bool uploadTexture(rendering::TextureId texture,
                     const rendering::TextureUploadData& upload) override;
  void destroyTexture(rendering::TextureId texture) override;

  rendering::RenderTargetId createRenderTarget(const rendering::RenderTargetDesc& desc) override;
  void destroyRenderTarget(rendering::RenderTargetId target) override;

  rendering::TerrainId createTerrain(const rendering::TerrainDesc& desc) override;
  void destroyTerrain(rendering::TerrainId terrain) override;
  void uploadTerrainTile(rendering::TerrainId terrain,
                         const rendering::TerrainTileData& tile) override;
  void uploadTerrainMaterialLayer(rendering::TerrainId terrain,
                                  const rendering::TerrainMaterialLayerData& layer) override;
  void clearTerrainMaterialLayers(rendering::TerrainId terrain) override;
  void evictTerrainTile(rendering::TerrainId terrain,
                        rendering::TerrainTileCoord coord) override;
  void submitTerrain(const rendering::TerrainDrawItem& item) override;
  rendering::TerrainCapabilities getTerrainCapabilities() const override;
  rendering::TerrainStats getTerrainStats() const override;

  rendering::DeformationId createDeformation(const rendering::DeformationDesc& desc) override;
  void updateDeformation(rendering::DeformationId deformation,
                         const rendering::DeformationDesc& desc) override;
  void destroyDeformation(rendering::DeformationId deformation) override;
  rendering::DeformationStats getDeformationStats() const override;

  void submit(const rendering::DrawItem& item) override;
  void submitInstanced(const rendering::InstancedDrawItem& item) override;
  void submitParticles(rendering::ParticleBatch batch) override;
  void submitPackedParticles(rendering::PackedParticleBatch batch) override;
  void submitParticleEmitter(const rendering::ParticleEmitterGpuDesc& emitter) override;
  void submitParticleBeam(const rendering::ParticleBeamGpuDesc& beam) override;
  void setParticleSystemStats(const rendering::ParticlePassStats& stats) override;
  void retireInstance(rendering::InstanceId instance) override;
  void renderLayer(rendering::LayerId layer,
                   rendering::RenderTargetId target,
                   const rendering::FrameGraphDesc& frame_graph) override;
  void drawLine(const math::Vec3& start, const math::Vec3& end,
                const math::Color& color, bool depth_test, float thickness) override;

  unsigned int getRenderTargetTextureId(rendering::RenderTargetId target) const override;

  void setCamera(const rendering::CameraData& camera) override;
  void setCameraActive(bool active) override;
  void setDirectionalLight(const rendering::DirectionalLightData& light) override;
  void setLights(const std::vector<rendering::LightData>& lights) override;
  void setEnvironmentMap(const std::filesystem::path& path, float intensity,
                         bool draw_skybox) override;
  void setClearColor(const math::Color& color) override;
  void setVsync(bool enabled) override;
  void setAnisotropy(bool enabled, int level) override;
  void setGenerateMips(bool enabled) override;
  void setForwardPlusSettings(int tile_size,
                              int max_lights_per_tile,
                              int max_local_lights) override;
  rendering::ForwardPlusStats getForwardPlusStats() const override;
  void setInstancingCpuTimings(float render_system_extraction_ms,
                               float forward_state_collection_ms) override;
  rendering::InstancingStats getInstancingStats() const override;
  rendering::ParticlePassStats getParticlePassStats() const override;
  rendering::RendererCommandStats getRendererCommandStats() const override;
  rendering::RendererFrameTimingStats getRendererFrameTimingStats() const override;
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
  void updateTextureRGBA8(rendering::TextureId texture, int w, int h, const void* pixels) override;
  void renderUi(const karma::rendering::UIDrawData& draw_data) override;

  Diligent::IRenderDevice* getDevice() const { return device_; }
  Diligent::IDeviceContext* getContext() const { return context_; }
  Diligent::ISwapChain* getSwapChain() const { return swap_chain_; }

 private:
  struct MeshRecord {
    world::MeshData data;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> vertex_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> index_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> morph_delta_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> morph_delta_srv;
    Diligent::Uint32 vertex_count = 0;
    Diligent::Uint32 index_count = 0;
    Diligent::Uint32 morph_target_count = 0;
    glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 bounds_center{0.0f, 0.0f, 0.0f};
    float bounds_radius = 0.0f;
    std::vector<ParticleGpuMeshSample> particle_source_samples;
    struct Submesh {
      Diligent::Uint32 index_offset = 0;
      Diligent::Uint32 index_count = 0;
      uint32_t material_slot = 0;
      rendering::MaterialId material = rendering::kInvalidMaterial;
    };
    std::vector<Submesh> submeshes;
    std::vector<rendering::MaterialId> owned_materials;
  };

  void refreshSubmeshesFromMeshData(MeshRecord& record);

  struct MaterialRecord {
    static constexpr size_t kTextureCoordSlotCount = 12;

    rendering::MaterialPipelineDesc pipeline;
    rendering::MaterialDesc desc;
    glm::vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 emissive_factor{0.0f, 0.0f, 0.0f};
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float emissive_strength = 1.0f;
    float clearcoat_factor = 0.0f;
    float clearcoat_roughness_factor = 0.0f;
    glm::vec3 sheen_color_factor{0.0f, 0.0f, 0.0f};
    float sheen_roughness_factor = 0.0f;
    float anisotropy_factor = 0.0f;
    float transmission_factor = 0.0f;
    float ior = 1.5f;
    float thickness_factor = 0.0f;
    float attenuation_distance = std::numeric_limits<float>::infinity();
    glm::vec3 attenuation_color{1.0f, 1.0f, 1.0f};
    MaterialPipelineKind shading_model = MaterialPipelineKind::Standard;
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
    glm::vec3 volume_axis_x{1.0f, 0.0f, 0.0f};
    glm::vec3 volume_axis_y{0.0f, 1.0f, 0.0f};
    glm::vec3 volume_axis_z{0.0f, 0.0f, 1.0f};
    uint32_t volume_shape = 0u;
    float volume_radius = 1.0f;
    float volume_capsule_half_length = 0.0f;
    float volume_density = 1.0f;
    float volume_scattering = 1.0f;
    float volume_anisotropy = 0.0f;
    float volume_absorption = 0.0f;
    float volume_distortion_strength = 0.0f;
    float volume_noise_strength = 1.0f;
    std::array<glm::vec4, 7> custom_material_params{};
    std::array<bool, 7> custom_material_param_enabled{};
    rendering::MaterialDesc::BlendMode blend_mode = rendering::MaterialDesc::BlendMode::Alpha;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> base_color_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> normal_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> metallic_roughness_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> occlusion_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> emissive_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> clearcoat_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> clearcoat_roughness_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> clearcoat_normal_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> sheen_color_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> sheen_roughness_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> transmission_srv;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> thickness_srv;
    std::array<glm::vec4, kTextureCoordSlotCount> texcoord_row0{};
    std::array<glm::vec4, kTextureCoordSlotCount> texcoord_row1{};
    static constexpr size_t kForwardSrbSlotCount = 14u;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> transparent_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> transparent_double_sided_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> additive_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> additive_double_sided_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> custom_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> custom_transparent_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> custom_transparent_double_sided_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> custom_additive_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> custom_additive_double_sided_srb;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shadow_alpha_srb;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, kForwardSrbSlotCount>
        layout_srbs;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, kForwardSrbSlotCount>
        layout_custom_srbs;
  };

  struct ImportedMaterialTemplateCacheEntry {
    std::vector<MaterialRecord> materials;
  };

  struct TextureRecord {
    rendering::TextureDesc desc;
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> srv;
  };

  struct RenderTargetRecord {
    rendering::RenderTargetDesc desc;
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
    rendering::LayerId layer = 0;
    rendering::MeshId mesh = rendering::kInvalidMesh;
    rendering::MaterialId material = rendering::kInvalidMaterial;
    std::vector<rendering::DrawMaterialBinding> materials;
    std::vector<std::string> render_tags;
    rendering::DeformationId deformation = rendering::kInvalidDeformation;
    glm::mat4 transform{1.0f};
    glm::vec4 params{0.0f};
    rendering::VolumeDrawParams volume_params{};
    bool has_volume_params = false;
    bool requires_scene_sample = false;
    bool post_particle_scene_sample = false;
    bool visible = true;
    bool shadow_visible = true;
    bool transform_changed = true;
  };

  struct InstancedRecord {
    struct LodRecord {
      float start_distance = 0.0f;
      rendering::MeshId mesh = rendering::kInvalidMesh;
      rendering::MaterialId material = rendering::kInvalidMaterial;
      std::vector<rendering::DrawMaterialBinding> materials;
      rendering::InstanceLodRenderMode render_mode = rendering::InstanceLodRenderMode::Mesh;
      glm::vec3 bounds_center{0.0f};
      float bounds_radius = 0.0f;
      bool bounds_valid = false;
      bool shadow_visible = false;
      Diligent::RefCntAutoPtr<Diligent::IBuffer> gpu_culled_instance_buffer;
      Diligent::RefCntAutoPtr<Diligent::IBufferView> gpu_culled_instance_uav;
      size_t gpu_culled_instance_buffer_capacity_bytes = 0;
      Diligent::RefCntAutoPtr<Diligent::IBuffer> gpu_culling_indirect_args_buffer;
      Diligent::RefCntAutoPtr<Diligent::IBufferView> gpu_culling_indirect_args_uav;
    };

    rendering::LayerId layer = 0;
    rendering::MeshId mesh = rendering::kInvalidMesh;
    rendering::MaterialId material = rendering::kInvalidMaterial;
    std::vector<rendering::DrawMaterialBinding> materials;
    std::vector<std::string> render_tags;
    std::vector<LodRecord> lods;
    rendering::InstanceGpuLayout gpu_layout = rendering::InstanceGpuLayout::Matrix4x4Params;
    std::vector<rendering::InstanceData> instances;
    std::vector<rendering::PlanarInstanceData> planar_instances;
    uint64_t revision = 0;
    glm::vec3 bounds_center{0.0f};
    float bounds_radius = 0.0f;
    bool bounds_valid = false;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> instance_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> instance_srv;
    size_t instance_buffer_capacity_bytes = 0;
    bool instance_buffer_dirty = true;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> gpu_culled_instance_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> gpu_culled_instance_uav;
    size_t gpu_culled_instance_buffer_capacity_bytes = 0;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> gpu_culling_indirect_args_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> gpu_culling_indirect_args_uav;
    bool dynamic = false;
    bool visible = true;
    bool shadow_visible = true;

    size_t instanceCount() const {
      switch (gpu_layout) {
        case rendering::InstanceGpuLayout::Matrix4x4Params:
          return instances.size();
        case rendering::InstanceGpuLayout::PositionYawScaleParams:
          return planar_instances.size();
      }
      return 0u;
    }
  };

  struct DeformationRecord {
    rendering::DeformationDesc desc;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> joint_palette_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> joint_palette_srv;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> morph_weight_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBufferView> morph_weight_srv;
    size_t joint_capacity = 0;
    size_t morph_weight_capacity = 0;
  };

  struct TerrainTileCoordHash {
    std::size_t operator()(const rendering::TerrainTileCoord& coord) const noexcept {
      uint64_t h = 0x517CC1B727220A95ull;
      h ^= static_cast<uint32_t>(coord.x) + 0x9E3779B97F4A7C15ull + (h << 6u) + (h >> 2u);
      h ^= static_cast<uint32_t>(coord.z) + 0x9E3779B97F4A7C15ull + (h << 6u) + (h >> 2u);
      return static_cast<std::size_t>(h);
    }
  };

  struct TerrainTileRecord {
    rendering::TerrainTileCoord coord{};
    Diligent::RefCntAutoPtr<Diligent::ITexture> height_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> height_srv;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> color_srv;
    Diligent::RefCntAutoPtr<Diligent::ITexture> control_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> control_srv;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> patch_vertex_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> cpu_vertex_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> cpu_index_buffer;
    std::unordered_map<uint64_t, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>>
        tess_srbs;
    std::unordered_map<uint64_t, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>>
        cpu_srbs;
    Diligent::Uint32 patch_vertex_count = 0u;
    Diligent::Uint32 cpu_index_count = 0u;
    float min_height = 0.0f;
    float max_height = 0.0f;
  };

  struct TerrainMaterialLayerRecord {
    std::string name;
    float uv_scale = 16.0f;
    bool enabled = false;
    Diligent::RefCntAutoPtr<Diligent::ITexture> albedo_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> albedo_srv;
    Diligent::RefCntAutoPtr<Diligent::ITexture> normal_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> normal_srv;
    Diligent::RefCntAutoPtr<Diligent::ITexture> roughness_texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> roughness_srv;
  };

  struct TerrainRecord {
    rendering::TerrainDesc desc{};
    std::unordered_map<rendering::TerrainTileCoord, TerrainTileRecord, TerrainTileCoordHash> tiles;
    std::array<TerrainMaterialLayerRecord, 4> material_layers{};
    uint32_t material_layer_count = 0u;
  };

  struct TerrainSubmission {
    rendering::TerrainDrawItem item{};
  };

  struct TerrainPipelineSet {
    Diligent::TEXTURE_FORMAT rtv_format = Diligent::TEX_FORMAT_UNKNOWN;
    Diligent::TEXTURE_FORMAT dsv_format = Diligent::TEX_FORMAT_UNKNOWN;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> tess_pipeline_state;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> cpu_pipeline_state;
  };

  struct ForwardBatchKey {
    rendering::MeshId mesh = rendering::kInvalidMesh;
    rendering::MaterialId material = rendering::kInvalidMaterial;
    Diligent::Uint32 index_offset = 0;
    Diligent::Uint32 index_count = 0;
    bool indexed = false;
    bool deformed = false;
    rendering::InstanceGpuLayout gpu_layout = rendering::InstanceGpuLayout::Matrix4x4Params;
    rendering::InstanceLodRenderMode render_mode = rendering::InstanceLodRenderMode::Mesh;

    bool operator==(const ForwardBatchKey& other) const {
      return mesh == other.mesh &&
             material == other.material &&
             index_offset == other.index_offset &&
             index_count == other.index_count &&
             indexed == other.indexed &&
             deformed == other.deformed &&
             gpu_layout == other.gpu_layout &&
             render_mode == other.render_mode;
    }
  };

  struct ForwardBatch {
    ForwardBatchKey key{};
    std::vector<rendering::InstanceData> instances;
    rendering::InstanceId instanced_record = rendering::kInvalidInstance;
    Diligent::Uint32 persistent_instance_count = 0;
    uint32_t instanced_lod_index = UINT32_MAX;
  };

  struct TransparentForwardDraw {
    enum class SceneSampleMode : uint32_t {
      None = 0,
      ReflectionOverlay = 1,
      Required = 2,
    };

    ForwardBatchKey key{};
    glm::mat4 transform{1.0f};
    glm::vec4 params{0.0f};
    rendering::VolumeDrawParams volume_params{};
    bool has_volume_params = false;
    rendering::DeformationId deformation = rendering::kInvalidDeformation;
    float depth = 0.0f;
    SceneSampleMode scene_sample_mode = SceneSampleMode::None;
  };

  struct DeformedForwardDraw {
    ForwardBatchKey key{};
    glm::mat4 transform{1.0f};
    glm::vec4 params{0.0f};
    rendering::DeformationId deformation = rendering::kInvalidDeformation;
  };

  struct ForwardLayerState {
    std::vector<ForwardBatch> opaque_batches;
    std::vector<DeformedForwardDraw> deformed_opaque_draws;
    std::vector<TransparentForwardDraw> scene_reflection_draws;
    std::vector<TransparentForwardDraw> transparent_draws;
    std::vector<TransparentForwardDraw> pre_particle_scene_sample_draws;
    std::vector<TransparentForwardDraw> post_particle_draws;
  };

  struct ForwardLayerStats {
    Diligent::Uint32 skipped_hidden = 0;
    Diligent::Uint32 skipped_missing_vb = 0;
    Diligent::Uint32 skipped_missing_mesh = 0;
    Diligent::Uint32 skipped_layer = 0;
    Diligent::Uint32 instanced_culled_batches = 0;
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
    rendering::LayerId layer = 0;
    bool depth_test = true;
    rendering::TextureId texture = rendering::kInvalidTexture;
    rendering::ParticleBlendMode blend_mode = rendering::ParticleBlendMode::Additive;
    rendering::ParticleAlignment alignment = rendering::ParticleAlignment::Billboard;
    rendering::ParticleShadingMode shading_mode = rendering::ParticleShadingMode::Standard;
    rendering::ParticlePresentationMode presentation_mode =
        rendering::ParticlePresentationMode::Baked;
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
    std::vector<rendering::ParticlePackedInstance> particles;
  };

  struct ParticleEmitterRuntimeState {
    float elapsed_seconds = 0.0f;
    float previous_elapsed_seconds = 0.0f;
    uint32_t gpu_slot_offset = 0u;
    uint32_t gpu_slot_capacity = 0u;
    uint32_t gpu_emitter_state_index = 0u;
    uint32_t restart_count = 0u;
    bool initialized = false;
    bool gpu_reset_pending = true;
    bool gpu_emitter_state_allocated = false;
  };

  struct ParticleEmitterSubmission {
    rendering::ParticleEmitterGpuDesc desc{};
    float elapsed_seconds = 0.0f;
    float previous_elapsed_seconds = 0.0f;
  };

  struct ParticleBeamRuntimeState {
    float elapsed_seconds = 0.0f;
    uint32_t restart_count = 0u;
    bool initialized = false;
  };

  struct ParticleBeamSubmission {
    rendering::ParticleBeamGpuDesc desc{};
    float elapsed_seconds = 0.0f;
  };

  struct ParticleBeamVertex {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float uv[2] = {0.0f, 0.0f};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  };

  struct ParticleGpuSlotRange {
    uint32_t offset = 0u;
    uint32_t capacity = 0u;
  };

  struct ParticleGlobalPipeline {
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
    Diligent::IShaderResourceVariable* materials_vs_var = nullptr;
    Diligent::IShaderResourceVariable* materials_ps_var = nullptr;
    Diligent::IShaderResourceVariable* textures_var = nullptr;
    Diligent::IShaderResourceVariable* scene_color_var = nullptr;
    Diligent::IShaderResourceVariable* scene_depth_var = nullptr;
  };

  enum class ForwardPipelineVariant {
    Opaque,
    OpaqueDoubleSided,
    DepthPrepass,
    Transparent,
    TransparentDoubleSided,
    Additive,
    AdditiveDoubleSided,
  };
  static constexpr size_t kForwardPipelineVariantCount = 7u;
  static constexpr size_t kInstanceGpuLayoutCount = 2u;
  static constexpr size_t kForwardSrbSlotCount =
      kForwardPipelineVariantCount * kInstanceGpuLayoutCount;
  static size_t forwardPipelineVariantIndex(ForwardPipelineVariant variant);
  static size_t instanceGpuLayoutIndex(rendering::InstanceGpuLayout layout);

  struct CustomForwardPipeline {
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    bool attempted = false;
  };

  void initializeDevice();
  void clearFrame(const float* color, bool clear_depth);
  void recreateShadowMap();
  void recreatePointShadowMap();
  void recreateRenderTargetResources(RenderTargetRecord& record, int width, int height);
  void recreateShadowPipeline();
  bool ensureCameraOverridePipeline(const rendering::CameraData& camera);
  void updateCameraOverrideUserConstants(const rendering::CameraData& camera);
  void ensureUiResources();
  void ensureLineResources();
  void ensureParticleBeamResources();
  TerrainPipelineSet* ensureTerrainResources(Diligent::TEXTURE_FORMAT rtv_format,
                                             Diligent::TEXTURE_FORMAT dsv_format);
  void ensureParticleResources();
  uint32_t activeRasterSampleCount() const;
  void setActiveRasterSampleCount(uint32_t sample_count);
  void releaseRasterSampleDependentResources();
  uint32_t effectiveMsaaSampleCount(Diligent::TEXTURE_FORMAT color_format,
                                    Diligent::TEXTURE_FORMAT depth_format,
                                    uint32_t requested_samples);
  bool ensureCameraRasterResources(int width,
                                   int height,
                                   Diligent::TEXTURE_FORMAT color_format,
                                   Diligent::TEXTURE_FORMAT depth_format,
                                   uint32_t sample_count);
  bool ensureResolvedDepthResource(int width, int height);
  bool ensureDepthResolvePipeline(Diligent::TEXTURE_FORMAT output_depth_format);
  bool resolveMsaaCameraResources(Diligent::ITexture* msaa_color_texture,
                                  Diligent::ITexture* resolved_color_texture,
                                  Diligent::ITextureView* resolved_depth_dsv,
                                  Diligent::TEXTURE_FORMAT color_format,
                                  Diligent::TEXTURE_FORMAT depth_format,
                                  uint32_t sample_count,
                                  int width,
                                  int height);
  bool ensureSsaaDownsamplePipeline(Diligent::TEXTURE_FORMAT color_format,
                                    Diligent::TEXTURE_FORMAT depth_format);
  bool runSsaaDownsample(Diligent::ITextureView* source_color_srv,
                         Diligent::ITextureView* source_depth_srv,
                         Diligent::ITextureView* output_rtv,
                         Diligent::ITextureView* output_dsv,
                         Diligent::TEXTURE_FORMAT color_format,
                         Diligent::TEXTURE_FORMAT depth_format,
                         int source_width,
                         int source_height,
                         int output_width,
                         int output_height);
  bool ensureFullscreenBlitPipeline(Diligent::TEXTURE_FORMAT format);
  bool runFullscreenBlit(Diligent::ITextureView* source_srv,
                         Diligent::ITextureView* target_rtv,
                         int target_width,
                         int target_height,
                         Diligent::TEXTURE_FORMAT format);
  uint32_t renderParticleBeams(rendering::LayerId layer, const ParticlePassContext& context);
  void recordRenderLayerStageTiming(const char* stage, double ms);
  void recordResourceCreation(const char* area,
                              const char* stage,
                              core::SteadyClock::time_point start,
                              core::SteadyClock::time_point end);
  void recordPipelineCreation(const char* area,
                              const char* stage,
                              core::SteadyClock::time_point start,
                              core::SteadyClock::time_point end);
  Diligent::IPipelineState* ensureForwardPipeline(
      ForwardPipelineVariant variant,
      rendering::InstanceGpuLayout layout = rendering::InstanceGpuLayout::Matrix4x4Params);
  Diligent::IPipelineState* ensureCustomForwardPipeline(const MaterialRecord& material,
                                                        ForwardPipelineVariant variant,
                                                        rendering::InstanceGpuLayout layout =
                                                            rendering::InstanceGpuLayout::
                                                                Matrix4x4Params);
  void bindForwardPipelineStaticResources(Diligent::IPipelineState* pso) const;
  bool materialUsesCustomForwardPipeline(const MaterialRecord& material) const;
  Diligent::IShaderResourceBinding* ensureMaterialForwardSrb(MaterialRecord& material,
                                                             ForwardPipelineVariant variant,
                                                             bool custom_pipeline,
                                                             rendering::InstanceGpuLayout layout =
                                                                 rendering::InstanceGpuLayout::
                                                                     Matrix4x4Params);
  Diligent::IShaderResourceBinding* ensureMaterialShadowAlphaSrb(MaterialRecord& material);
  bool ensureInstancedRecordBuffer(InstancedRecord& record);
  bool instancedGpuCullingEnabled() const;
  bool ensureInstancedGpuCullingResources();
  bool ensureInstancedGpuLodCullingResources();
  bool ensureInstancedGpuCullingOutputBuffers(
      size_t instance_count,
      Diligent::RefCntAutoPtr<Diligent::IBuffer>& visible_buffer,
      Diligent::RefCntAutoPtr<Diligent::IBufferView>& visible_uav,
      size_t& visible_buffer_capacity_bytes,
      Diligent::RefCntAutoPtr<Diligent::IBuffer>& indirect_args_buffer,
      Diligent::RefCntAutoPtr<Diligent::IBufferView>& indirect_args_uav);
  bool ensureInstancedGpuCullingRecordBuffers(InstancedRecord& record);
  void initializeMaterialBindingForPipeline(
      MaterialRecord& record,
      Diligent::IPipelineState* pso,
      Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb);
  void bindForwardPlusResourcesToSrb(Diligent::IShaderResourceBinding* srb) const;
  void initializeDefaultMaterialBinding(
      Diligent::IPipelineState* pso,
      Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb);
  void ensureDefaultSceneResources(int width, int height);
  void ensureParticleSceneCopyResources(int width,
                                        int height,
                                        Diligent::TEXTURE_FORMAT format);
  void ensureParticleHalfResAlphaResources(int width,
                                           int height,
                                           Diligent::TEXTURE_FORMAT format);
  void ensureParticleFallbackDepthResource();
  void applyPostProcessSettingsForPass(const rendering::PostProcessSettings& settings);
  void ensurePostProcessResources(int width,
                                  int height,
                                  Diligent::TEXTURE_FORMAT format);
  bool ensurePostProcessPipelines(Diligent::TEXTURE_FORMAT format);
  void applyPostProcessChain(Diligent::ITexture* scene_texture,
                             Diligent::ITextureView* scene_rtv,
                             Diligent::ITextureView* scene_depth_srv,
                             int width,
                             int height,
                             Diligent::TEXTURE_FORMAT format);
  bool runPostProcessPass(PostProcessPassResources& pass,
                          Diligent::ITextureView* source_srv,
                          Diligent::ITextureView* depth_srv,
                          Diligent::ITextureView* bloom_srv,
                          Diligent::ITextureView* history_srv,
                          Diligent::ITextureView* target_rtv,
                          int width,
                          int height,
                          bool history_valid);
  bool executeFrameGraphScreenPasses(
      const rendering::FrameGraphDesc& graph,
      rendering::LayerId layer,
      Diligent::ITexture* camera_color_texture,
      Diligent::ITextureView* camera_color_srv,
      Diligent::ITextureView* camera_depth_srv,
      Diligent::ITextureView* camera_color_rtv,
      const DrawConstants& base_constants,
      int width,
      int height,
      Diligent::TEXTURE_FORMAT color_format,
      rendering::RenderTargetId target);
  bool ensureFrameGraphShaderPassPipeline(
      const rendering::ShaderPassAssetDesc& asset,
      const rendering::FrameGraphPassDesc& pass,
      Diligent::TEXTURE_FORMAT color_format,
      FrameGraphShaderPassResources& out_pass);
  bool ensureFrameGraphSceneMaskPipeline(
      Diligent::TEXTURE_FORMAT color_format,
      Diligent::TEXTURE_FORMAT depth_format,
      rendering::InstanceGpuLayout layout,
      FrameGraphSceneMaskResources& out_pass);
  Diligent::ITextureView* runBloomChain(Diligent::ITextureView* source_srv,
                                        int width,
                                        int height,
                                        Diligent::TEXTURE_FORMAT format);
  void uploadMeshBuffers(const world::MeshData& mesh, MeshRecord& record);
  void uploadMeshMorphBuffers(const world::MeshData& mesh, MeshRecord& record);
  bool ensureFallbackDeformationResources();
  bool bindDeformationResources(Diligent::IShaderResourceBinding* srb,
                                const MeshRecord& mesh,
                                rendering::DeformationId deformation);
  bool updateDeformationConstants(const MeshRecord& mesh,
                                  rendering::DeformationId deformation);
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
  Diligent::RefCntAutoPtr<Diligent::ITextureView> createSolidCubeTextureSRV(
      unsigned char r,
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
  Diligent::RefCntAutoPtr<Diligent::ITextureView> loadImportedMaterialTexture(
      const rendering::ImportedMaterialTexture& texture);
  Diligent::RefCntAutoPtr<Diligent::ITextureView> loadTextureFromFile(const std::filesystem::path& path,
                                                                      bool srgb,
                                                                      const char* label);
  rendering::MaterialId createMaterialFromImportedPayload(
      const std::filesystem::path& path,
      uint32_t material_index,
      const rendering::ImportedMaterialData& imported);
  MaterialRecord buildImportedMaterialRecord(const aiScene& scene,
                                             const aiMaterial& material,
                                             const std::filesystem::path& asset_path);
  MaterialRecord buildImportedMaterialRecord(const rendering::ImportedMaterialData& material);
  void applyResolvedMaterial(MaterialRecord& record,
                             const rendering::ResolvedMaterialDesc& resolved);
  void initializeMaterialBindings(MaterialRecord& record);
  void initializeTextureCoordTransforms(MaterialRecord& record) const;
  void setTextureCoordTransform(MaterialRecord& record,
                                const aiMaterial& material,
                                unsigned int texture_type,
                                unsigned int texture_index,
                                unsigned int uv_index,
                                size_t slot) const;
  void bindShadowResourcesToSrb(Diligent::IShaderResourceBinding* srb) const;
  Diligent::ITextureView* defaultBrdfLutSrv() const;
  Diligent::ITextureView* brdfLutSrv() const;
  void bindEnvironmentResources();
  void preloadAssimpTextures(const aiScene& scene, const std::filesystem::path& asset_path);
  void preloadImportedMaterialTextures(const rendering::ImportedMaterialData& material);
  const ImportedMaterialTemplateCacheEntry* getImportedMaterialTemplates(
      const std::filesystem::path& path);
  void ensureEnvironmentResources();
  void renderSkybox(const glm::mat4& projection, const glm::mat4& view);
  struct RenderStateCacheFileInfo {
    bool exists = false;
    std::uintmax_t size = 0;
  };
	  RenderStateCacheFileInfo renderStateCacheFileInfo() const;
	  void saveRenderStateCache(std::string_view reason);
  void initializePipelineStateCache();
  void savePipelineStateCache(std::string_view reason);
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> createGraphicsPipelineState(
      Diligent::GraphicsPipelineStateCreateInfo create_info);
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> createComputePipelineState(
      Diligent::ComputePipelineStateCreateInfo create_info);
	  void applyDiligentPresentEnvironment() const;

  karma::platform::Window* window_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
  Diligent::RefCntAutoPtr<Diligent::ISwapChain> swap_chain_;
  Diligent::RenderDeviceWithCache<false> device_with_cache_;
  std::filesystem::path render_state_cache_path_;
  std::filesystem::path pipeline_state_cache_path_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineStateCache> pipeline_state_cache_;
  bool shader_cache_enabled_ = true;
  bool pipeline_cache_enabled_ = true;
  bool shader_cache_log_ = false;
  std::uint32_t shader_cache_version_ = 30;
  bool shader_cache_flush_ = false;
  Diligent::RefCntAutoPtr<Diligent::IShader> forward_vs_;
  Diligent::RefCntAutoPtr<Diligent::IShader> forward_ps_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> opaque_double_sided_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> depth_prepass_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> transparent_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> transparent_double_sided_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> additive_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> additive_double_sided_pipeline_state_;
  std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, kForwardPipelineVariantCount>
      compact_forward_pipeline_states_;
  std::unordered_map<std::string, CustomForwardPipeline> custom_forward_pipelines_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> camera_override_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> camera_override_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> camera_override_user_constants_;
  std::filesystem::path camera_override_vertex_path_;
  std::filesystem::path camera_override_fragment_path_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> shadow_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> shadow_alpha_pipeline_state_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shader_resources_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> depth_prepass_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>
      opaque_double_sided_default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> transparent_default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>
      transparent_double_sided_default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> additive_default_material_srb_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>
      additive_double_sided_default_material_srb_;
  std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>,
             kForwardPipelineVariantCount>
      compact_default_material_srbs_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shadow_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> deformation_constants_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> fallback_joint_palette_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> fallback_joint_palette_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> fallback_morph_weight_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> fallback_morph_weight_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> fallback_morph_delta_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> fallback_morph_delta_srv_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_color_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_color_clamp_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> sampler_data_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> terrain_color_sampler_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> terrain_height_sampler_;
  Diligent::RefCntAutoPtr<Diligent::ISampler> terrain_material_sampler_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> terrain_default_control_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> terrain_default_control_;
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
    std::array<rendering::LightData, kMaxPointShadowLights> point_shadow_lights{};
    std::array<size_t, kMaxPointShadowLights> point_shadow_light_source_indices{};
    std::array<size_t, kMaxPointShadowLights> point_shadow_local_light_indices{};
    Diligent::Uint32 point_shadow_light_count = 0;
    bool point_shadow_ready = false;
  };
  void renderShadowLayer(rendering::LayerId layer,
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
  void collectForwardLayerState(rendering::LayerId layer,
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
                                            int render_height,
                                            bool is_gl);
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
  void renderParticlePasses(rendering::LayerId layer, const ParticlePassContext& context);
  Diligent::Uint32 renderTerrainLayer(rendering::LayerId layer,
                                      const DrawConstants& base_constants,
                                      const glm::mat4& view_proj,
                                      bool is_gl,
                                      Diligent::ITextureView* active_rtv,
                                      Diligent::ITextureView* active_dsv,
                                      int render_width,
                                      int render_height);
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
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> instanced_gpu_culling_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> instanced_gpu_culling_srb_;
  Diligent::IShaderResourceVariable* instanced_gpu_culling_source_var_ = nullptr;
  Diligent::IShaderResourceVariable* instanced_gpu_culling_visible_var_ = nullptr;
  Diligent::IShaderResourceVariable* instanced_gpu_culling_args_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> instanced_gpu_culling_cb_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> instanced_gpu_lod_culling_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> instanced_gpu_lod_culling_srb_;
  Diligent::IShaderResourceVariable* instanced_gpu_lod_culling_source_var_ = nullptr;
  std::array<Diligent::IShaderResourceVariable*, 4> instanced_gpu_lod_culling_visible_vars_{};
  std::array<Diligent::IShaderResourceVariable*, 4> instanced_gpu_lod_culling_args_vars_{};
  Diligent::RefCntAutoPtr<Diligent::ISampler> ui_sampler_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> line_pipeline_state_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> line_pipeline_state_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> line_srb_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> line_srb_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> line_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> line_cb_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_beam_pipeline_additive_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_beam_pipeline_additive_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_beam_pipeline_alpha_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_beam_pipeline_alpha_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_beam_srb_additive_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_beam_srb_additive_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_beam_srb_alpha_depth_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_beam_srb_alpha_no_depth_;
  Diligent::IShaderResourceVariable* particle_beam_texture_var_additive_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_beam_texture_var_additive_no_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_beam_texture_var_alpha_depth_ = nullptr;
  Diligent::IShaderResourceVariable* particle_beam_texture_var_alpha_no_depth_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_beam_vb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_beam_cb_;
  std::unordered_map<uint64_t, TerrainPipelineSet> terrain_pipeline_sets_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> terrain_cb_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_additive_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_additive_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_alpha_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_alpha_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_alpha_half_res_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_distortion_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_pipeline_state_distortion_no_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_half_res_composite_pipeline_state_;
  ParticleGlobalPipeline particle_global_alpha_depth_;
  ParticleGlobalPipeline particle_global_alpha_no_depth_;
  ParticleGlobalPipeline particle_global_alpha_half_res_;
  ParticleGlobalPipeline particle_global_distortion_depth_;
  ParticleGlobalPipeline particle_global_distortion_no_depth_;
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
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_instance_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_cb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_sim_cb_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_sim_compute_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_sim_compute_srb_;
  Diligent::IShaderResourceVariable* particle_sim_output_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_frame_cb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_sort_cb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_emitter_desc_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_emitter_desc_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_emitter_state_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_emitter_state_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_emitter_state_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_state_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_state_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_state_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_alive_list_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_alive_list_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_dead_list_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_dead_list_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_group_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_group_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_material_record_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_material_record_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_mesh_sample_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_mesh_sample_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_group_counter_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_group_counter_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_group_counter_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_sort_item_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_sort_item_srv_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_sort_item_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_indirect_draw_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_indirect_draw_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_indirect_dispatch_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_indirect_dispatch_uav_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> particle_gpu_stats_buffer_;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> particle_gpu_stats_uav_;
  std::array<Diligent::RefCntAutoPtr<Diligent::IBuffer>, 2> particle_gpu_stats_readback_buffers_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_gpu_clear_compute_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_gpu_clear_compute_srb_;
  Diligent::IShaderResourceVariable* particle_gpu_clear_groups_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_clear_counters_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_clear_sort_items_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_clear_draw_args_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_clear_dispatch_args_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_clear_stats_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_gpu_update_emitters_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_gpu_update_emitters_srb_;
  Diligent::IShaderResourceVariable* particle_gpu_update_emitters_descs_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_update_emitters_states_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_gpu_simulate_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_gpu_simulate_srb_;
  Diligent::IShaderResourceVariable* particle_gpu_simulate_descs_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_simulate_emitters_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_simulate_states_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_simulate_alive_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_simulate_dead_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_simulate_stats_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_simulate_mesh_samples_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_gpu_prepare_unsorted_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_gpu_prepare_unsorted_srb_;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_unsorted_descs_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_unsorted_states_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_unsorted_groups_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_unsorted_counters_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_unsorted_instances_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_unsorted_stats_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_gpu_generate_sort_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_gpu_generate_sort_srb_;
  Diligent::IShaderResourceVariable* particle_gpu_generate_sort_descs_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_generate_sort_states_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_generate_sort_groups_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_generate_sort_counters_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_generate_sort_items_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_generate_sort_stats_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_gpu_sort_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_gpu_sort_srb_;
  Diligent::IShaderResourceVariable* particle_gpu_sort_items_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_gpu_prepare_sorted_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_gpu_prepare_sorted_srb_;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_sorted_states_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_sorted_groups_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_sorted_counters_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_sorted_sort_items_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_sorted_instances_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_prepare_sorted_stats_var_ = nullptr;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> particle_gpu_indirect_args_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> particle_gpu_indirect_args_srb_;
  Diligent::IShaderResourceVariable* particle_gpu_indirect_args_groups_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_indirect_args_counters_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_indirect_args_draw_args_var_ = nullptr;
  Diligent::IShaderResourceVariable* particle_gpu_indirect_args_stats_var_ = nullptr;
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
  PostProcessTexture camera_raster_color_;
  PostProcessTexture camera_raster_depth_;
  PostProcessTexture camera_resolved_depth_;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> depth_resolve_pso_;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> depth_resolve_srb_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> depth_resolve_cb_;
  Diligent::IShaderResourceVariable* depth_resolve_source_var_ = nullptr;
  Diligent::TEXTURE_FORMAT depth_resolve_pipeline_depth_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  PostProcessPassResources ssaa_downsample_pass_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> ssaa_downsample_cb_;
  Diligent::TEXTURE_FORMAT ssaa_downsample_color_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::TEXTURE_FORMAT ssaa_downsample_depth_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  PostProcessPassResources fullscreen_blit_pass_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> post_process_ping_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> post_process_ping_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> post_process_ping_rtv_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> post_process_pong_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> post_process_pong_srv_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> post_process_pong_rtv_;
  std::array<Diligent::RefCntAutoPtr<Diligent::ITexture>, 2> post_process_history_tex_;
  std::array<Diligent::RefCntAutoPtr<Diligent::ITextureView>, 2> post_process_history_srv_;
  std::array<Diligent::RefCntAutoPtr<Diligent::ITextureView>, 2> post_process_history_rtv_;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> post_process_cb_;
  PostProcessPassResources post_process_bloom_prefilter_pass_;
  PostProcessPassResources post_process_bloom_downsample_pass_;
  PostProcessPassResources post_process_bloom_upsample_pass_;
  PostProcessPassResources post_process_composite_pass_;
  PostProcessPassResources post_process_temporal_pass_;
  std::vector<PostProcessTexture> post_process_bloom_mips_;
  std::vector<PostProcessTexture> post_process_bloom_scratch_mips_;
  int post_process_width_ = 0;
  int post_process_height_ = 0;
  Diligent::TEXTURE_FORMAT post_process_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::TEXTURE_FORMAT post_process_pipeline_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::TEXTURE_FORMAT fullscreen_blit_pipeline_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  int post_process_history_index_ = 0;
  bool post_process_history_valid_ = false;
  std::unordered_map<std::string, PostProcessTexture> frame_graph_color_textures_;
  std::unordered_map<std::string, PostProcessTexture> frame_graph_depth_textures_;
  std::unordered_map<std::string, FrameGraphShaderPassResources> frame_graph_shader_passes_;
  std::unordered_map<std::string, FrameGraphSceneMaskResources> frame_graph_scene_mask_passes_;
  PostProcessTexture frame_graph_source_copy_;
  Diligent::TEXTURE_FORMAT frame_graph_source_copy_format_ =
      Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_base_color_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_normal_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_metallic_roughness_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_brdf_lut_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_occlusion_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_emissive_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITexture> default_env_tex_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_base_color_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_normal_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_metallic_roughness_;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> default_brdf_lut_;
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

  rendering::MeshId nextMeshId_ = 1;
  rendering::MaterialId nextMaterialId_ = 1;
  rendering::TextureId nextTextureId_ = 1;
  rendering::RenderTargetId nextTargetId_ = 1;
  rendering::TerrainId nextTerrainId_ = 1;
  rendering::DeformationId nextDeformationId_ = 1;

  std::unordered_map<rendering::MeshId, MeshRecord> meshes_;
  std::unordered_map<rendering::MaterialId, MaterialRecord> materials_;
  std::unordered_map<std::string, ImportedMaterialTemplateCacheEntry> imported_material_templates_;
  std::unordered_map<std::string, MaterialRecord> imported_payload_material_templates_;
  std::unordered_map<rendering::TextureId, TextureRecord> textures_;
  std::unordered_map<std::string, rendering::TextureId> texture_cache_;
  std::unordered_map<rendering::RenderTargetId, RenderTargetRecord> targets_;
  std::unordered_map<rendering::TerrainId, TerrainRecord> terrains_;
  std::unordered_map<rendering::DeformationId, DeformationRecord> deformations_;
  std::unordered_map<rendering::InstanceId, InstanceRecord> instances_;
  std::unordered_map<rendering::InstanceId, InstancedRecord> instanced_records_;
  std::vector<TerrainSubmission> terrain_submissions_;
  std::vector<ParticleBatchRecord> particle_batches_;
  std::vector<ParticleEmitterSubmission> particle_emitter_submissions_;
  std::unordered_map<uint64_t, ParticleEmitterRuntimeState> particle_emitter_runtime_states_;
  std::vector<ParticleBeamSubmission> particle_beam_submissions_;
  std::unordered_map<uint64_t, ParticleBeamRuntimeState> particle_beam_runtime_states_;
  std::vector<LineVertex> line_vertices_depth_;
  std::vector<LineVertex> line_vertices_no_depth_;

  rendering::CameraData camera_{};
  bool camera_active_ = true;
  float clear_color_[4] = {0.2f, 0.6f, 1.0f, 1.0f};
  rendering::DirectionalLightData directional_light_{};
  std::vector<rendering::LightData> lights_;
  std::filesystem::path environment_map_;
  float environment_intensity_ = 0.0f;
	  bool draw_skybox_ = true;
	  bool vsync_enabled_ = true;
	  rendering::PresentMode present_mode_ = rendering::PresentMode::Auto;
	  int env_debug_mode_ = 0;
  bool debug_glossy_off_ = false;
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
  rendering::PostProcessSettings post_process_settings_{};
  int point_shadow_map_size_ = 1024;
  int point_shadow_max_lights_ = 2;
  size_t ui_vb_size_ = 0;
  size_t ui_ib_size_ = 0;
  size_t instance_vb_capacity_ = 0;
  size_t forward_plus_light_capacity_ = 0;
  size_t forward_plus_tile_count_capacity_ = 0;
  size_t forward_plus_tile_index_capacity_ = 0;
  size_t line_vb_size_ = 0;
  size_t particle_beam_vb_size_ = 0;
  size_t particle_instance_capacity_ = 0;
  size_t particle_gpu_emitter_desc_capacity_ = 0;
  size_t particle_gpu_emitter_state_capacity_ = 0;
  size_t particle_gpu_state_capacity_ = 0;
  size_t particle_gpu_allocated_capacity_ = 0;
  size_t particle_gpu_high_water_capacity_ = 0;
  size_t particle_gpu_emitter_state_allocated_capacity_ = 0;
  size_t particle_gpu_alive_list_capacity_ = 0;
  size_t particle_gpu_dead_list_capacity_ = 0;
  size_t particle_gpu_group_capacity_ = 0;
  size_t particle_gpu_material_record_capacity_ = 0;
  size_t particle_gpu_mesh_sample_capacity_ = 0;
  size_t particle_gpu_group_counter_capacity_ = 0;
  size_t particle_gpu_sort_capacity_ = 0;
  size_t particle_gpu_indirect_draw_capacity_ = 0;
  size_t particle_gpu_indirect_dispatch_capacity_ = 0;
  size_t particle_gpu_stats_capacity_ = 0;
  uint32_t particle_gpu_stats_readback_frame_ = 0u;
  uint32_t particle_gpu_stats_readback_age_ = 0u;
  bool particle_gpu_stats_readback_valid_ = false;
  ParticleGpuStatsReadback particle_gpu_last_stats_{};
  std::vector<ParticleGpuSlotRange> particle_gpu_free_particle_slots_;
  std::vector<uint32_t> particle_gpu_free_emitter_state_slots_;
  int default_scene_width_ = 0;
  int default_scene_height_ = 0;
  uint32_t active_raster_sample_count_ = 1u;
  bool warned_msaa_downgrade_ = false;
  int particle_scene_color_copy_width_ = 0;
  int particle_scene_color_copy_height_ = 0;
  Diligent::TEXTURE_FORMAT particle_scene_color_copy_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  int particle_half_res_alpha_width_ = 0;
  int particle_half_res_alpha_height_ = 0;
  Diligent::TEXTURE_FORMAT particle_half_res_alpha_format_ = Diligent::TEX_FORMAT_UNKNOWN;
  int forward_plus_tile_size_ = 16;
  int forward_plus_max_lights_per_tile_ = 128;
  rendering::ForwardPlusStats forward_plus_stats_{};
  rendering::InstancingStats instancing_stats_{};
  rendering::ParticlePassStats particle_pass_stats_{};
  rendering::TerrainStats terrain_stats_{};
  rendering::RendererFrameTimingStats current_frame_timing_stats_{};
  rendering::RendererFrameTimingStats last_frame_timing_stats_{};
  rendering::ParticlePassStats particle_stats_log_totals_{};
  double particle_stats_log_elapsed_seconds_ = 0.0;
  float last_frame_delta_seconds_ = 0.0f;
  uint32_t particle_stats_log_frame_count_ = 0;
  bool particle_stats_log_initialized_ = false;
  bool particle_stats_log_enabled_ = false;
  bool warned_instanced_gpu_culling_unsupported_ = false;
  bool frame_active_ = false;
  bool present_frame_ = true;
  bool warned_line_thickness_ = false;
  int current_width_ = 0;
  int current_height_ = 0;
  bool warned_no_draws_ = false;
  static constexpr rendering::TextureId kRenderTargetTextureHandleBit = 0x80000000u;
  int forward_plus_max_local_lights_ = 4096;

  bool directional_shadow_cache_valid_ = false;
  bool directional_shadow_scene_dirty_ = false;
  bool point_shadow_scene_dirty_ = false;
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

}  // namespace karma::rendering::backend
