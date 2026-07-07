#pragma once

#include "karma/core.h"
#include "karma/math.h"
#include "karma/platform.h"
#include "karma/world.h"



#include <cstdint>
#include <limits>

namespace karma::rendering {

/// \ingroup karma_rendering
/// Stable renderer instance handle used for submitted draw records.
using InstanceId = uint64_t;
/// \ingroup karma_rendering
/// Opaque mesh resource handle.
using MeshId = uint32_t;
/// \ingroup karma_rendering
/// Opaque material resource handle.
using MaterialId = uint32_t;
/// \ingroup karma_rendering
/// Opaque texture resource handle.
using TextureId = uint32_t;
/// \ingroup karma_rendering
/// Opaque render-target resource handle.
using RenderTargetId = uint32_t;
/// \ingroup karma_rendering
/// Opaque streamed terrain resource handle.
using TerrainId = uint32_t;
/// \ingroup karma_rendering
/// Opaque renderer-owned mesh deformation resource handle.
using DeformationId = uint32_t;
/// \ingroup karma_rendering
/// Render layer id used to submit and draw batches independently.
using LayerId = uint32_t;

constexpr RenderTargetId kDefaultRenderTarget = 0;
constexpr MaterialId kInvalidMaterial = 0;
constexpr MeshId kInvalidMesh = 0;
constexpr TextureId kInvalidTexture = 0;
constexpr TerrainId kInvalidTerrain = 0;
constexpr DeformationId kInvalidDeformation = 0;
constexpr InstanceId kInvalidInstance = std::numeric_limits<InstanceId>::max();

}  // namespace karma::rendering


namespace karma::rendering {

/// \ingroup karma_rendering
/// Per-frame renderer viewport and timing data.
struct FrameInfo {
  int width = 0;
  int height = 0;
  float delta_time = 0.0f;
  bool present = true;
};

}  // namespace karma::rendering


namespace karma::rendering {

/// \ingroup karma_rendering
/// Render target creation descriptor.
struct RenderTargetDesc {
  int width = 0;
  int height = 0;
  bool depth = true;
  bool stencil = false;
};

}  // namespace karma::rendering


#include <cstddef>
#include <cstdint>
#include <vector>

namespace karma::rendering {

/// \ingroup karma_rendering
/// CPU texture upload format.
enum class TextureFormat {
  RGBA8,
  RGB8,
  R8,
  BC7_RGBA_UNORM,
  BC7_RGBA_UNORM_SRGB,
  KTX2_BASIS_UASTC
};

/// \ingroup karma_rendering
/// Texture creation descriptor.
struct TextureDesc {
  int width = 0;
  int height = 0;
  TextureFormat format = TextureFormat::RGBA8;
  bool srgb = false;
  bool generate_mips = false;
  uint32_t mip_levels = 1u;
};

/// \ingroup karma_rendering
/// One mip/subresource inside a prepared texture upload.
struct TextureUploadSubresource {
  uint32_t mip_level = 0u;
  uint32_t array_layer = 0u;
  int width = 0;
  int height = 0;
  std::size_t offset = 0u;
  std::size_t size = 0u;
  std::size_t row_stride = 0u;
};

/// \ingroup karma_rendering
/// CPU-side texture bytes prepared for backend upload.
struct TextureUploadData {
  TextureFormat format = TextureFormat::RGBA8;
  std::vector<TextureUploadSubresource> subresources;
  std::vector<std::uint8_t> bytes;
};

}  // namespace karma::rendering


namespace karma::rendering {

/// \ingroup karma_rendering
/// Renderer-owned image-space effects applied after scene rendering.
///
/// All effects are opt-in. Backends may implement individual effects with
/// different quality/performance tradeoffs, but unsupported features should be
/// ignored rather than changing scene rendering semantics.
///
/// Settings are normally registered in `assets::AssetRegistry` and selected
/// by `CameraComponent::post_process_profile_key`. `RenderSystem` resolves the
/// active settings per camera pass before calling the backend.
struct PostProcessSettings {
  /// Master switch for the profile. When false, backends skip post processing.
  bool enabled = true;

  /// Enables bloom prefilter/downsample/upsample composition.
  bool bloom_enabled = false;
  float bloom_threshold = 1.0f;
  float bloom_intensity = 0.25f;
  float bloom_radius = 1.0f;

  /// Enables final tone/color mapping in the display composite path.
  bool tone_mapping_enabled = false;
  float tone_exposure = 1.0f;
  float tone_contrast = 1.0f;
  float tone_saturation = 1.0f;

  /// Enables depth-derived ambient occlusion in the current composite path.
  bool ssao_enabled = false;
  float ssao_radius = 1.5f;
  float ssao_intensity = 0.35f;
  float ssao_power = 1.4f;

  /// Enables screen-space reflections in the current composite path.
  bool screen_space_reflections_enabled = false;
  float ssr_intensity = 0.25f;
  float ssr_max_roughness = 0.75f;
  float ssr_thickness = 0.08f;

  /// Enables temporal history blending where the backend has history resources.
  bool temporal_antialiasing_enabled = false;
  float taa_feedback = 0.92f;
  float taa_sharpening = 0.08f;

  /// Enables depth-of-field controls in the current composite path.
  bool depth_of_field_enabled = false;
  float dof_focus_depth = 8.0f;
  float dof_focus_range = 4.0f;
  float dof_intensity = 1.0f;
};

}  // namespace karma::rendering


#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>


#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace karma::rendering {

/// \ingroup karma_rendering
/// Maximum number of camera shader color parameters.
static constexpr uint32_t kCameraShaderUserParamCapacity = 32;

/// Hashes a camera shader parameter key with FNV-1a.
inline uint32_t cameraShaderParamKeyHash(std::string_view key) {
  uint32_t hash = 2166136261u;
  for (char c : key) {
    hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
    hash *= 16777619u;
  }
  return hash;
}

/// \ingroup karma_rendering
/// One hashed camera shader parameter.
struct CameraShaderUserParam {
  uint32_t key_hash = 0u;
  math::Color value{};
};

/// \ingroup karma_rendering
/// Renderer-facing camera state extracted from ECS camera/transform data.
struct CameraData {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  bool perspective = true;
  bool render_shadows = true;
  float fov_y_degrees = 60.0f;
  float aspect = 1.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
  float ortho_left = -1.0f;
  float ortho_right = 1.0f;
  float ortho_top = 1.0f;
  float ortho_bottom = -1.0f;
  std::filesystem::path shader_override_vertex_path;
  std::filesystem::path shader_override_fragment_path;
  std::array<CameraShaderUserParam, kCameraShaderUserParamCapacity> shader_user_params{};
  uint32_t shader_user_param_count = 0;
};

}  // namespace karma::rendering



namespace karma::rendering {

/// \ingroup karma_rendering
/// World-space ray generated from a screen point and camera state.
struct ScreenRay {
  math::Vec3 origin{};
  math::Vec3 direction{};
};

/// Converts a screen-space point into a world-space camera ray.
bool screenPointToWorldRay(double screen_x,
                           double screen_y,
                           int viewport_width,
                           int viewport_height,
                           const math::Vec3& camera_position,
                           const math::Quat& camera_rotation,
                           float fov_y_degrees,
                           ScreenRay& out_ray);

}  // namespace karma::rendering


#include <cstdint>


#include <glm/glm.hpp>

namespace karma::rendering {

/// \ingroup karma_rendering
/// Renderer-facing directional light state.
struct DirectionalLightData {
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  float shadow_extent = 0.0f;
  bool casts_shadows = false;
};

/// \ingroup karma_rendering
/// Renderer-facing local/directional light kind.
enum class LightType : uint32_t {
  Directional = 0,
  Point = 1,
  Spot = 2
};

/// \ingroup karma_rendering
/// Renderer-facing point/spot/directional light record.
struct LightData {
  LightType type = LightType::Point;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 10.0f;
  float inner_cone_cos = 0.9659258f;
  float outer_cone_cos = 0.8660254f;
  bool casts_shadows = false;
};

}  // namespace karma::rendering


#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace karma::rendering {

/// GPU vertex-input layout used by instanced mesh batches.
enum class InstanceGpuLayout : uint32_t {
  Matrix4x4Params = 0,
  PositionYawScaleParams = 1,
};

/// Draw-time orientation behavior for an instanced LOD mesh.
enum class InstanceLodRenderMode : uint32_t {
  Mesh = 0,
  UprightBillboard = 1,
};

/// Per-instance GPU payload for the default matrix layout.
struct alignas(16) InstanceData {
  glm::mat4 transform{1.0f};
  glm::vec4 params{0.0f};
};

/// Compact planar/yaw-only instance payload.
struct alignas(16) PlanarInstanceData {
  glm::vec4 position_yaw{0.0f};
  glm::vec4 scale_pad{1.0f, 1.0f, 1.0f, 0.0f};
  glm::vec4 params{0.0f};
};

inline constexpr size_t instanceGpuLayoutStride(InstanceGpuLayout layout) {
  switch (layout) {
    case InstanceGpuLayout::Matrix4x4Params:
      return sizeof(InstanceData);
    case InstanceGpuLayout::PositionYawScaleParams:
      return sizeof(PlanarInstanceData);
  }
  return sizeof(InstanceData);
}

}  // namespace karma::rendering



#include <span>
#include <vector>

namespace karma::rendering {

/// Non-owning material binding for one mesh material slot.
struct DrawMaterialBinding {
  uint32_t slot = 0;
  MaterialId material = kInvalidMaterial;
};

/// \ingroup karma_rendering
/// Per-draw analytic volume metadata exposed to custom material shaders.
struct VolumeDrawParams {
  glm::vec3 center{0.0f, 0.0f, 0.0f};
  glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
  glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
  glm::vec3 axis_z{0.0f, 0.0f, 1.0f};
  float radius = 1.0f;
  float capsule_half_length = 0.0f;
  uint32_t shape = 0u;
  uint32_t slot = 0u;
  float overlay_depth = 0.0f;
  bool surface_double_sided = false;
};

/// \ingroup karma_rendering
/// One renderable mesh submission.
///
/// `RenderSystem` builds draw items from ECS mesh/deformation data. Runtime
/// modules can submit draw items directly when they own renderer resources.
struct DrawItem {
  InstanceId instance = kInvalidInstance;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  std::vector<DrawMaterialBinding> materials;
  DeformationId deformation = kInvalidDeformation;
  glm::mat4 transform{1.0f};
  glm::vec4 instance_params{0.0f};
  VolumeDrawParams volume_params{};
  bool has_volume_params = false;
  bool requires_scene_sample = false;
  bool post_particle_scene_sample = false;
  LayerId layer = 0;
  bool visible = true;
  bool shadow_visible = true;
};

struct InstancedLodDrawDesc {
  float start_distance = 0.0f;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  std::vector<DrawMaterialBinding> materials;
  InstanceLodRenderMode render_mode = InstanceLodRenderMode::Mesh;
  glm::vec3 bounds_center{0.0f};
  float bounds_radius = 0.0f;
  bool bounds_valid = false;
  bool shadow_visible = false;
};

/// \ingroup karma_rendering
/// One batch of repeated mesh instances with shared mesh/material bindings.
struct InstancedDrawItem {
  InstanceId instance = kInvalidInstance;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  std::vector<DrawMaterialBinding> materials;
  std::vector<InstancedLodDrawDesc> lods;
  InstanceGpuLayout gpu_layout = InstanceGpuLayout::Matrix4x4Params;
  std::span<const InstanceData> instances;
  std::span<const PlanarInstanceData> planar_instances;
  bool payload_changed = true;
  uint64_t revision = 0;
  glm::vec3 bounds_center{0.0f};
  float bounds_radius = 0.0f;
  bool bounds_valid = false;
  LayerId layer = 0;
  bool dynamic = false;
  bool visible = true;
  bool shadow_visible = true;

  size_t instanceCount() const {
    switch (gpu_layout) {
      case InstanceGpuLayout::Matrix4x4Params:
        return instances.size();
      case InstanceGpuLayout::PositionYawScaleParams:
        return planar_instances.size();
    }
    return 0u;
  }
};

}  // namespace karma::rendering


#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>


#include <glm/glm.hpp>

namespace karma::rendering {

/// \ingroup karma_rendering
/// Alias for renderer color values.
using Color = math::Color;

/// \ingroup karma_rendering
/// Standard material surface and render-state data.
struct MaterialDesc {
  /// Transparent blending mode for material draws.
  enum class BlendMode : uint32_t {
    Alpha = 0,
    Additive = 1,
  };

  /// Alpha handling for standard surface rendering.
  enum class AlphaMode : uint32_t {
    Opaque = 0,
    Masked = 1,
    Blend = 2,
  };

  math::Color base_color{1.0f, 1.0f, 1.0f, 1.0f};
  math::Color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
  float metallic = 1.0f;
  float roughness = 1.0f;
  float normal_scale = 1.0f;
  float occlusion_strength = 1.0f;
  float emissive_strength = 1.0f;
  float clearcoat = 0.0f;
  float clearcoat_roughness = 0.0f;
  math::Color sheen_color{0.0f, 0.0f, 0.0f, 1.0f};
  float sheen_roughness = 0.0f;
  float anisotropy = 0.0f;
  float transmission = 0.0f;
  float ior = 1.5f;
  float thickness = 0.0f;
  float attenuation_distance = std::numeric_limits<float>::infinity();
  math::Color attenuation_color{1.0f, 1.0f, 1.0f, 1.0f};
  bool analytic_sphere_normals = false;
  bool unlit = false;
  AlphaMode alpha_mode = AlphaMode::Opaque;
  float alpha_cutoff = 0.5f;
  float alpha_softness = 0.0f;
  bool alpha_dither = false;
  bool alpha_to_coverage = false;
  bool transparent = false;
  BlendMode blend_mode = BlendMode::Alpha;
  bool depth_test = true;
  bool depth_write = true;
  bool wireframe = false;
  bool double_sided = false;
};

/// Texture transform slot count used by imported glTF-style materials.
inline constexpr size_t kImportedMaterialTextureCoordSlotCount = 12u;

/// Renderer-facing semantic for an imported material texture.
enum class ImportedMaterialTextureSemantic : uint32_t {
  BaseColor = 0,
  Normal,
  MetallicRoughness,
  Occlusion,
  Emissive,
  Clearcoat,
  ClearcoatRoughness,
  ClearcoatNormal,
  SheenColor,
  SheenRoughness,
  Transmission,
  Thickness,
};

/// Source texture reference captured by a content importer for renderer upload.
struct ImportedMaterialTexture {
  ImportedMaterialTextureSemantic semantic = ImportedMaterialTextureSemantic::BaseColor;
  std::string source_key;
  std::string raw_name;
  std::filesystem::path resolved_path;
  std::string label;
  std::vector<uint8_t> source_bytes;
  uint32_t width = 0u;
  uint32_t height = 0u;
  bool embedded = false;
  bool compressed = true;
  bool srgb = false;
};

/// Imported material data captured while the source scene is already loaded.
struct ImportedMaterialData {
  ImportedMaterialData() {
    resetTextureTransforms();
  }

  void resetTextureTransforms() {
    for (size_t i = 0; i < kImportedMaterialTextureCoordSlotCount; ++i) {
      texcoord_row0[i] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
      texcoord_row1[i] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    }
  }

  MaterialDesc material{};
  std::vector<ImportedMaterialTexture> textures;
  std::array<glm::vec4, kImportedMaterialTextureCoordSlotCount> texcoord_row0{};
  std::array<glm::vec4, kImportedMaterialTextureCoordSlotCount> texcoord_row1{};
};

/// Pipeline family for a material asset.
struct MaterialPipelineDesc {
  std::string name = "standard";
  std::filesystem::path vertex_shader_path;
  std::filesystem::path fragment_shader_path;
  std::string vertex_entry_point = "main";
  std::string fragment_entry_point = "main";
  std::vector<std::string> defines;
};

/// Named material parameter value used by material assets and variants.
using MaterialParameterValue =
    std::variant<bool,
                 int32_t,
                 uint32_t,
                 float,
                 Color,
                 glm::vec2,
                 glm::vec3,
                 glm::vec4,
                 std::string>;

/// Shared material asset definition registered by key.
struct MaterialAssetDesc {
  std::string material_key;
  MaterialPipelineDesc pipeline{};
  MaterialDesc surface{};
  std::unordered_map<std::string, MaterialParameterValue> params;
  std::unordered_map<std::string, std::string> textures;
  std::filesystem::path material_asset_path;
  uint32_t material_asset_index = std::numeric_limits<uint32_t>::max();
  std::shared_ptr<const ImportedMaterialData> imported_material;
};

/// Material variant definition registered by key.
///
/// A variant inherits a base material's pipeline, surface, textures, and import
/// payload, then applies local params and texture assignments. Variants are just
/// materials: any mesh slot can be assigned either an asset key or a variant key.
struct MaterialVariantDesc {
  std::string material_key;
  std::string base_material_key;
  std::unordered_map<std::string, MaterialParameterValue> params;
  std::unordered_map<std::string, std::string> textures;
};

/// Flattened renderer-facing material after asset/variant inheritance.
struct ResolvedMaterialDesc {
  MaterialPipelineDesc pipeline{};
  MaterialDesc surface{};
  std::unordered_map<std::string, MaterialParameterValue> params;
  std::unordered_map<std::string, std::string> textures;
  std::unordered_map<std::string, TextureId> texture_handles;
  std::filesystem::path material_asset_path;
  uint32_t material_asset_index = std::numeric_limits<uint32_t>::max();
  std::shared_ptr<const ImportedMaterialData> imported_material;

  static ResolvedMaterialDesc fromSurface(MaterialDesc material) {
    ResolvedMaterialDesc desc{};
    desc.surface = std::move(material);
    return desc;
  }
};

/// \ingroup karma_rendering
/// Convenience settings for a standard non-metal diffuse material.
struct DiffuseMaterialDesc {
  math::Color base_color{1.0f, 1.0f, 1.0f, 1.0f};
  float roughness = 1.0f;
  bool double_sided = false;
  bool unlit = false;
};

/// Creates a standard non-metal diffuse material surface.
inline MaterialDesc createDiffuseMaterial(const DiffuseMaterialDesc& desc = {}) {
  MaterialDesc material{};
  material.base_color = desc.base_color;
  material.metallic = 0.0f;
  material.roughness = desc.roughness;
  material.double_sided = desc.double_sided;
  material.unlit = desc.unlit;
  if (desc.base_color.a < 1.0f) {
    material.alpha_mode = MaterialDesc::AlphaMode::Blend;
    material.transparent = true;
    material.depth_write = false;
  }
  return material;
}

/// Creates a standard non-metal diffuse material surface.
inline MaterialDesc createDiffuseMaterial(math::Color base_color, float roughness = 1.0f) {
  DiffuseMaterialDesc desc{};
  desc.base_color = base_color;
  desc.roughness = roughness;
  return createDiffuseMaterial(desc);
}

/// Creates a material asset descriptor for a standard non-metal diffuse material.
inline MaterialAssetDesc createDiffuseMaterialAsset(std::string material_key,
                                                   const DiffuseMaterialDesc& desc = {}) {
  MaterialAssetDesc asset{};
  asset.material_key = std::move(material_key);
  asset.surface = createDiffuseMaterial(desc);
  return asset;
}

/// Creates a material asset descriptor for a standard non-metal diffuse material.
inline MaterialAssetDesc createDiffuseMaterialAsset(std::string material_key,
                                                   math::Color base_color,
                                                   float roughness = 1.0f) {
  DiffuseMaterialDesc desc{};
  desc.base_color = base_color;
  desc.roughness = roughness;
  return createDiffuseMaterialAsset(std::move(material_key), desc);
}

}  // namespace karma::rendering


#include <vector>

#include <glm/glm.hpp>


namespace karma::rendering {

/// \ingroup karma_rendering
/// Renderer-owned deformation payload for one draw-time mesh instance.
///
/// Joint matrices are final mesh-space skinning matrices. Morph weights are
/// indexed by the target order stored on `world::MeshData::morph_targets`.
struct DeformationDesc {
  std::vector<glm::mat4> joint_palette;
  std::vector<float> morph_weights;
  bool skinning_enabled = false;
  bool morphing_enabled = false;
};

/// \ingroup karma_rendering
/// Runtime deformation resource counters for diagnostics and debug overlays.
struct DeformationStats {
  uint32_t resource_count = 0;
  uint32_t joint_matrix_count = 0;
  uint32_t morph_weight_count = 0;
};

}  // namespace karma::rendering


#include <cstdint>
#include <vector>


#include <glm/glm.hpp>

namespace karma::rendering {

/// \ingroup karma_rendering
/// Particle blend path selected by particle emitters/batches.
enum class ParticleBlendMode : uint32_t {
  Additive = 0,
  Alpha = 1,
  Distortion = 2,
};

/// \ingroup karma_rendering
/// Particle orientation mode.
enum class ParticleAlignment : uint32_t {
  Billboard = 0,
  Ground = 1,
};

/// \ingroup karma_rendering
/// Particle shader family.
enum class ParticleShadingMode : uint32_t {
  Standard = 0,
  Shell = 1,
};

/// \ingroup karma_rendering
/// Particle source shape for renderer-owned particle simulation.
enum class ParticleSourceShape : uint32_t {
  Box = 0,
  Sphere = 1,
  SphereSurface = 2,
  Disc = 3,
  Ring = 4,
  Cylinder = 5,
  Capsule = 6,
  Cone = 7,
  Line = 8,
  Path = 9,
  TrailPath = 10,
  MeshSurface = 11,
};

/// \ingroup karma_rendering
/// Particle source sampling policy.
enum class ParticleSourceSamplingMode : uint32_t {
  Random = 0,
  Sequential = 1,
  Vertices = 2,
};

/// \ingroup karma_rendering
/// Particle source emission distribution.
enum class ParticleSourceDistribution : uint32_t {
  Uniform = 0,
  Surface = 1,
  Edge = 2,
};

/// \ingroup karma_rendering
/// Whether particle presentation was pre-baked or should be evaluated on GPU.
enum class ParticlePresentationMode : uint32_t {
  Baked = 0,
  Simulated = 1,
};

/// \ingroup karma_rendering
/// Compatibility particle instance with fully evaluated presentation values.
struct ParticleInstance {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  float size = 1.0f;
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float rotation_radians = 0.0f;
  glm::vec2 uv_min{0.0f, 0.0f};
  glm::vec2 uv_max{1.0f, 1.0f};
  glm::vec2 uv_min_next{0.0f, 0.0f};
  glm::vec2 uv_max_next{1.0f, 1.0f};
  float frame_blend = 0.0f;
  math::Color color_end{1.0f, 1.0f, 1.0f, 1.0f};
  float size_end = 1.0f;
  float normalized_age = 0.0f;
  float age_seconds = 0.0f;
  uint32_t frame_offset = 0u;
};

/// \ingroup karma_rendering
/// Packed particle instance uploaded to the renderer's particle path.
struct alignas(16) ParticlePackedInstance {
  float position_age[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float color_start[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float color_end[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float rotation_size[4] = {1.0f, 0.0f, 1.0f, 1.0f};
  float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  float uv_rect_next[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// \ingroup karma_rendering
/// Compatibility particle batch submitted by non-simulated producers.
struct ParticleBatch {
  LayerId layer = 0;
  bool depth_test = true;
  TextureId texture = kInvalidTexture;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  ParticleAlignment alignment = ParticleAlignment::Billboard;
  ParticleShadingMode shading_mode = ParticleShadingMode::Standard;
  ParticlePresentationMode presentation_mode = ParticlePresentationMode::Baked;
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
  std::vector<ParticleInstance> particles;
};

/// \ingroup karma_rendering
/// Packed particle batch submitted by `ParticleSystem`.
struct PackedParticleBatch {
  LayerId layer = 0;
  bool depth_test = true;
  TextureId texture = kInvalidTexture;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  ParticleAlignment alignment = ParticleAlignment::Billboard;
  ParticleShadingMode shading_mode = ParticleShadingMode::Standard;
  ParticlePresentationMode presentation_mode = ParticlePresentationMode::Baked;
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
  std::vector<ParticlePackedInstance> particles;
};

/// \ingroup karma_rendering
/// Renderer-facing particle emitter submission for GPU-first effects.
///
/// This is intentionally plain data: feature systems resolve ECS bindings,
/// library assets, overrides, and transforms, then submit this descriptor to the
/// renderer backend. Backends own live particle state for the descriptor.
struct ParticleEmitterGpuDesc {
  uint64_t instance_id = 0;
  uint32_t restart_count = 0;
  float delta_seconds = 0.0f;
  bool visible = true;

  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};

  bool enabled = true;
  bool playing = true;
  bool loop = true;
  bool emit_burst_on_start = true;
  bool local_space = false;
  LayerId layer = 0;
  bool depth_test = true;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  ParticleAlignment alignment = ParticleAlignment::Billboard;
  ParticleShadingMode shading_mode = ParticleShadingMode::Standard;
  bool use_soft_mask = true;
  float soft_particle_distance = 0.0f;
  float distortion_strength = 0.0f;
  float fresnel_power = 4.0f;
  float fresnel_strength = 1.0f;
  float refraction_strength = 0.0f;
  float interior_glow = 0.0f;
  TextureId texture = kInvalidTexture;
  uint32_t atlas_columns = 1;
  uint32_t atlas_rows = 1;
  uint32_t atlas_frame_count = 0;
  uint32_t atlas_frame_width = 0;
  uint32_t atlas_frame_height = 0;
  uint32_t atlas_border_x = 0;
  uint32_t atlas_border_y = 0;
  uint32_t atlas_spacing_x = 0;
  uint32_t atlas_spacing_y = 0;
  float animation_fps = 0.0f;
  bool animate_over_lifetime = false;
  bool random_start_frame = false;
  uint32_t max_particles = 256;
  uint32_t burst_count = 0;
  uint32_t seed = 0;
  float time_scale = 1.0f;
  float start_delay = 0.0f;
  float duration = 0.0f;
  float spawn_rate = 32.0f;
  float particle_lifetime_min = 0.65f;
  float particle_lifetime_max = 1.15f;
  float start_size_min = 0.18f;
  float start_size_max = 0.32f;
  float end_size_min = 0.03f;
  float end_size_max = 0.10f;
  float size_curve_exponent = 1.0f;
  float alpha_curve_exponent = 1.0f;
  float initial_rotation_min = 0.0f;
  float initial_rotation_max = 6.2831853f;
  float angular_velocity_min = -1.5f;
  float angular_velocity_max = 1.5f;
  ParticleSourceShape source_shape = ParticleSourceShape::Box;
  math::Vec3 source_box_extents{0.0f, 0.0f, 0.0f};
  math::Vec3 source_dimensions{0.0f, 0.0f, 0.0f};
  float source_radius_min = 0.0f;
  float source_radius_max = 0.0f;
  float source_inner_radius = 0.0f;
  float source_outer_radius = 0.0f;
  float source_height = 0.0f;
  float source_angle = 0.0f;
  std::vector<math::Vec3> source_path_points;
  bool source_closed_loop = false;
  ParticleSourceSamplingMode source_sampling = ParticleSourceSamplingMode::Random;
  float source_jitter_radius = 0.0f;
  MeshId source_mesh = kInvalidMesh;
  math::Vec3 source_mesh_bounds_center{0.0f, 0.0f, 0.0f};
  float source_mesh_bounds_radius = 0.0f;
  ParticleSourceDistribution source_distribution = ParticleSourceDistribution::Uniform;
  float radial_speed_min = 0.0f;
  float radial_speed_max = 0.0f;
  math::Vec3 velocity_min{-0.6f, 2.5f, -0.6f};
  math::Vec3 velocity_max{0.6f, 4.5f, 0.6f};
  math::Vec3 acceleration{0.0f, -3.5f, 0.0f};
  float drag = 0.0f;
  math::Vec3 orbit_axis{0.0f, 1.0f, 0.0f};
  float orbit_speed = 0.0f;
  bool collide_with_ground = false;
  float ground_height = 0.0f;
  float bounce_damping = 0.35f;
  float collision_friction = 0.25f;
  float rest_speed_threshold = 0.35f;
  math::Color start_color{1.0f, 0.8f, 0.35f, 0.9f};
  math::Color end_color{1.0f, 0.15f, 0.05f, 0.0f};
};

/// \ingroup karma_rendering
/// Renderer-facing particle beam/ribbon submission.
struct ParticleBeamGpuDesc {
  uint64_t instance_id = 0;
  uint32_t restart_count = 0;
  float delta_seconds = 0.0f;
  bool visible = true;

  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};

  bool enabled = true;
  LayerId layer = 0;
  bool depth_test = true;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  TextureId texture = kInvalidTexture;
  std::vector<math::Vec3> local_path_points;
  float start_width = 0.2f;
  float end_width = 0.2f;
  math::Color start_color{1.0f, 1.0f, 1.0f, 1.0f};
  math::Color end_color{1.0f, 1.0f, 1.0f, 1.0f};
  float edge_softness = 0.0f;
  float uv_repeat = 1.0f;
  float uv_scroll_speed = 0.0f;
  float time_scale = 1.0f;
};

}  // namespace karma::rendering



#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace karma::rendering {

/// \ingroup karma_rendering
/// Particle simulation and renderer-pass diagnostics.
///
/// These fields are intentionally public so examples and perf logs can report
/// comparable counters without parsing backend internals.
struct ParticlePassStats {
  uint32_t effect_binding_updates = 0;
  uint32_t simulated_emitters = 0;
  uint32_t visible_emitters = 0;
  uint32_t culled_emitters = 0;
  uint32_t submitted_emitters = 0;
  uint32_t simulated_particles = 0;
  uint32_t packed_particles = 0;
  uint32_t culled_particles = 0;
  uint32_t ground_collision_particles = 0;
  uint32_t submitted_batches = 0;
  uint32_t submitted_particles = 0;
  uint32_t additive_batches = 0;
  uint32_t additive_particles = 0;
  uint32_t alpha_batches = 0;
  uint32_t alpha_particles = 0;
  uint32_t distortion_batches = 0;
  uint32_t distortion_particles = 0;
  uint32_t additive_draw_calls = 0;
  uint32_t alpha_draw_calls = 0;
  uint32_t distortion_draw_calls = 0;
  uint32_t alpha_sorted_particles = 0;
  uint32_t distortion_sorted_particles = 0;
  uint32_t alpha_invalid_depth_particles = 0;
  uint32_t distortion_invalid_depth_particles = 0;
  uint32_t pre_particle_scene_sample_draws = 0;
  uint32_t post_particle_scene_sample_draws = 0;
  uint32_t gpu_particle_capacity = 0;
  uint32_t gpu_alive_particles = 0;
  uint32_t gpu_dead_particles = 0;
  uint32_t gpu_spawned_particles = 0;
  uint32_t gpu_killed_particles = 0;
  uint32_t gpu_compacted_particles = 0;
  uint32_t gpu_compute_dispatches = 0;
  uint32_t gpu_indirect_draws = 0;
  uint32_t gpu_indirect_dispatches = 0;
  uint32_t gpu_sort_key_count = 0;
  uint32_t gpu_sort_passes = 0;
  uint32_t gpu_buffer_resizes = 0;
  uint32_t gpu_stats_readback_age = 0;
  uint32_t gpu_allocator_live_emitters = 0;
  uint32_t gpu_allocator_free_ranges = 0;
  uint32_t gpu_allocator_active_capacity = 0;
  uint32_t gpu_allocator_high_water_capacity = 0;
  uint32_t gpu_allocator_retired_emitters = 0;
  uint32_t gpu_allocator_reused_slots = 0;
  uint32_t gpu_allocator_allocation_failures = 0;
  uint32_t gpu_culled_emitters = 0;
  uint32_t gpu_culled_particles = 0;
  uint32_t gpu_culling_dispatches = 0;
  uint32_t cpu_fallback_particles = 0;
  uint32_t submitted_beams = 0;
  uint32_t beam_segments = 0;
  uint32_t beam_draw_calls = 0;
  float sync_effect_bindings_ms = 0.0f;
  float simulation_ms = 0.0f;
  float packing_ms = 0.0f;
  float additive_grouping_ms = 0.0f;
  float alpha_sort_ms = 0.0f;
  float distortion_sort_ms = 0.0f;
  float alpha_collect_ms = 0.0f;
  float alpha_sort_only_ms = 0.0f;
  float alpha_span_ms = 0.0f;
  float distortion_collect_ms = 0.0f;
  float distortion_sort_only_ms = 0.0f;
  float distortion_span_ms = 0.0f;
  float draw_submission_ms = 0.0f;
  bool scene_color_copy = false;
  bool post_particle_scene_color_copy = false;
  bool alpha_half_res = false;
  bool distortion_present = false;
  bool gpu_sort_overflow = false;
  bool gpu_fallback_active = false;
  bool gpu_global_sort_active = false;
  bool gpu_grouped_sort_fallback = false;
};

/// \ingroup karma_rendering
/// Accumulated particle statistics over a reporting window.
struct ParticleStatsReport {
  ParticlePassStats totals{};
  uint32_t frame_count = 0;
  double elapsed_seconds = 0.0;
};

/// \ingroup karma_rendering
/// Adds one frame of particle statistics into an aggregate report.
inline void accumulateParticleStats(ParticlePassStats& totals,
                                    const ParticlePassStats& frame) {
  totals.effect_binding_updates += frame.effect_binding_updates;
  totals.simulated_emitters += frame.simulated_emitters;
  totals.visible_emitters += frame.visible_emitters;
  totals.culled_emitters += frame.culled_emitters;
  totals.submitted_emitters += frame.submitted_emitters;
  totals.simulated_particles += frame.simulated_particles;
  totals.packed_particles += frame.packed_particles;
  totals.culled_particles += frame.culled_particles;
  totals.ground_collision_particles += frame.ground_collision_particles;
  totals.submitted_batches += frame.submitted_batches;
  totals.submitted_particles += frame.submitted_particles;
  totals.additive_batches += frame.additive_batches;
  totals.additive_particles += frame.additive_particles;
  totals.alpha_batches += frame.alpha_batches;
  totals.alpha_particles += frame.alpha_particles;
  totals.distortion_batches += frame.distortion_batches;
  totals.distortion_particles += frame.distortion_particles;
  totals.additive_draw_calls += frame.additive_draw_calls;
  totals.alpha_draw_calls += frame.alpha_draw_calls;
  totals.distortion_draw_calls += frame.distortion_draw_calls;
  totals.alpha_sorted_particles += frame.alpha_sorted_particles;
  totals.distortion_sorted_particles += frame.distortion_sorted_particles;
  totals.alpha_invalid_depth_particles += frame.alpha_invalid_depth_particles;
  totals.distortion_invalid_depth_particles += frame.distortion_invalid_depth_particles;
  totals.pre_particle_scene_sample_draws += frame.pre_particle_scene_sample_draws;
  totals.post_particle_scene_sample_draws += frame.post_particle_scene_sample_draws;
  totals.gpu_particle_capacity += frame.gpu_particle_capacity;
  totals.gpu_alive_particles += frame.gpu_alive_particles;
  totals.gpu_dead_particles += frame.gpu_dead_particles;
  totals.gpu_spawned_particles += frame.gpu_spawned_particles;
  totals.gpu_killed_particles += frame.gpu_killed_particles;
  totals.gpu_compacted_particles += frame.gpu_compacted_particles;
  totals.gpu_compute_dispatches += frame.gpu_compute_dispatches;
  totals.gpu_indirect_draws += frame.gpu_indirect_draws;
  totals.gpu_indirect_dispatches += frame.gpu_indirect_dispatches;
  totals.gpu_sort_key_count += frame.gpu_sort_key_count;
  totals.gpu_sort_passes += frame.gpu_sort_passes;
  totals.gpu_buffer_resizes += frame.gpu_buffer_resizes;
  totals.gpu_stats_readback_age += frame.gpu_stats_readback_age;
  totals.gpu_allocator_live_emitters += frame.gpu_allocator_live_emitters;
  totals.gpu_allocator_free_ranges += frame.gpu_allocator_free_ranges;
  totals.gpu_allocator_active_capacity += frame.gpu_allocator_active_capacity;
  totals.gpu_allocator_high_water_capacity += frame.gpu_allocator_high_water_capacity;
  totals.gpu_allocator_retired_emitters += frame.gpu_allocator_retired_emitters;
  totals.gpu_allocator_reused_slots += frame.gpu_allocator_reused_slots;
  totals.gpu_allocator_allocation_failures += frame.gpu_allocator_allocation_failures;
  totals.gpu_culled_emitters += frame.gpu_culled_emitters;
  totals.gpu_culled_particles += frame.gpu_culled_particles;
  totals.gpu_culling_dispatches += frame.gpu_culling_dispatches;
  totals.cpu_fallback_particles += frame.cpu_fallback_particles;
  totals.submitted_beams += frame.submitted_beams;
  totals.beam_segments += frame.beam_segments;
  totals.beam_draw_calls += frame.beam_draw_calls;
  totals.sync_effect_bindings_ms += frame.sync_effect_bindings_ms;
  totals.simulation_ms += frame.simulation_ms;
  totals.packing_ms += frame.packing_ms;
  totals.additive_grouping_ms += frame.additive_grouping_ms;
  totals.alpha_sort_ms += frame.alpha_sort_ms;
  totals.distortion_sort_ms += frame.distortion_sort_ms;
  totals.alpha_collect_ms += frame.alpha_collect_ms;
  totals.alpha_sort_only_ms += frame.alpha_sort_only_ms;
  totals.alpha_span_ms += frame.alpha_span_ms;
  totals.distortion_collect_ms += frame.distortion_collect_ms;
  totals.distortion_sort_only_ms += frame.distortion_sort_only_ms;
  totals.distortion_span_ms += frame.distortion_span_ms;
  totals.draw_submission_ms += frame.draw_submission_ms;
  totals.scene_color_copy = totals.scene_color_copy || frame.scene_color_copy;
  totals.post_particle_scene_color_copy =
      totals.post_particle_scene_color_copy || frame.post_particle_scene_color_copy;
  totals.alpha_half_res = totals.alpha_half_res || frame.alpha_half_res;
  totals.distortion_present = totals.distortion_present || frame.distortion_present;
  totals.gpu_sort_overflow = totals.gpu_sort_overflow || frame.gpu_sort_overflow;
  totals.gpu_fallback_active = totals.gpu_fallback_active || frame.gpu_fallback_active;
  totals.gpu_global_sort_active =
      totals.gpu_global_sort_active || frame.gpu_global_sort_active;
  totals.gpu_grouped_sort_fallback =
      totals.gpu_grouped_sort_fallback || frame.gpu_grouped_sort_fallback;
}

/// \ingroup karma_rendering
/// Formats a stable one-line terminal report matching the debug particle tab.
inline std::string formatParticleStatsReport(const ParticleStatsReport& report) {
  std::ostringstream stream;
  const double inv_frames =
      report.frame_count > 0u ? 1.0 / static_cast<double>(report.frame_count) : 0.0;
  const double fps = report.elapsed_seconds > 0.0
                         ? static_cast<double>(report.frame_count) / report.elapsed_seconds
                         : 0.0;
  const auto avg = [inv_frames](uint32_t value) {
    return static_cast<double>(value) * inv_frames;
  };
  const auto avg_ms = [inv_frames](float value) {
    return static_cast<double>(value) * inv_frames;
  };

  stream << std::fixed << std::setprecision(2)
         << "Particle stats: seconds=" << report.elapsed_seconds
         << " frames=" << report.frame_count
         << " fps=" << std::setprecision(1) << fps
         << " effect_binding_updates=" << avg(report.totals.effect_binding_updates)
         << " simulated_emitters=" << avg(report.totals.simulated_emitters)
         << " visible_emitters=" << avg(report.totals.visible_emitters)
         << " culled_emitters=" << avg(report.totals.culled_emitters)
         << " submitted_emitters=" << avg(report.totals.submitted_emitters)
         << " simulated_particles=" << avg(report.totals.simulated_particles)
         << " packed_particles=" << avg(report.totals.packed_particles)
         << " culled_particles=" << avg(report.totals.culled_particles)
         << " ground_collision_particles=" << avg(report.totals.ground_collision_particles)
         << " submitted_batches=" << avg(report.totals.submitted_batches)
         << " submitted_particles=" << avg(report.totals.submitted_particles)
         << " additive_batches=" << avg(report.totals.additive_batches)
         << " alpha_batches=" << avg(report.totals.alpha_batches)
         << " distortion_batches=" << avg(report.totals.distortion_batches)
         << " additive_particles=" << avg(report.totals.additive_particles)
         << " alpha_particles=" << avg(report.totals.alpha_particles)
         << " distortion_particles=" << avg(report.totals.distortion_particles)
         << " additive_draw_calls=" << avg(report.totals.additive_draw_calls)
         << " alpha_draw_calls=" << avg(report.totals.alpha_draw_calls)
         << " distortion_draw_calls=" << avg(report.totals.distortion_draw_calls)
         << " alpha_sorted_particles=" << avg(report.totals.alpha_sorted_particles)
         << " distortion_sorted_particles=" << avg(report.totals.distortion_sorted_particles)
         << " alpha_invalid_depth_particles=" << avg(report.totals.alpha_invalid_depth_particles)
         << " distortion_invalid_depth_particles="
         << avg(report.totals.distortion_invalid_depth_particles)
         << " pre_particle_scene_sample_draws="
         << avg(report.totals.pre_particle_scene_sample_draws)
         << " post_particle_scene_sample_draws="
         << avg(report.totals.post_particle_scene_sample_draws)
         << " gpu_particle_capacity=" << avg(report.totals.gpu_particle_capacity)
         << " gpu_alive_particles=" << avg(report.totals.gpu_alive_particles)
         << " gpu_dead_particles=" << avg(report.totals.gpu_dead_particles)
         << " gpu_spawned_particles=" << avg(report.totals.gpu_spawned_particles)
         << " gpu_killed_particles=" << avg(report.totals.gpu_killed_particles)
         << " gpu_compacted_particles=" << avg(report.totals.gpu_compacted_particles)
         << " gpu_compute_dispatches=" << avg(report.totals.gpu_compute_dispatches)
         << " gpu_indirect_draws=" << avg(report.totals.gpu_indirect_draws)
         << " gpu_indirect_dispatches=" << avg(report.totals.gpu_indirect_dispatches)
         << " gpu_sort_key_count=" << avg(report.totals.gpu_sort_key_count)
         << " gpu_sort_passes=" << avg(report.totals.gpu_sort_passes)
         << " gpu_buffer_resizes=" << avg(report.totals.gpu_buffer_resizes)
         << " gpu_stats_readback_age=" << avg(report.totals.gpu_stats_readback_age)
         << " gpu_allocator_live_emitters="
         << avg(report.totals.gpu_allocator_live_emitters)
         << " gpu_allocator_free_ranges="
         << avg(report.totals.gpu_allocator_free_ranges)
         << " gpu_allocator_active_capacity="
         << avg(report.totals.gpu_allocator_active_capacity)
         << " gpu_allocator_high_water_capacity="
         << avg(report.totals.gpu_allocator_high_water_capacity)
         << " gpu_allocator_retired_emitters="
         << avg(report.totals.gpu_allocator_retired_emitters)
         << " gpu_allocator_reused_slots="
         << avg(report.totals.gpu_allocator_reused_slots)
         << " gpu_allocator_allocation_failures="
         << avg(report.totals.gpu_allocator_allocation_failures)
         << " gpu_culled_emitters=" << avg(report.totals.gpu_culled_emitters)
         << " gpu_culled_particles=" << avg(report.totals.gpu_culled_particles)
         << " gpu_culling_dispatches=" << avg(report.totals.gpu_culling_dispatches)
         << " cpu_fallback_particles=" << avg(report.totals.cpu_fallback_particles)
         << " submitted_beams=" << avg(report.totals.submitted_beams)
         << " beam_segments=" << avg(report.totals.beam_segments)
         << " beam_draw_calls=" << avg(report.totals.beam_draw_calls)
         << std::setprecision(3)
         << " sync_effect_bindings_ms=" << avg_ms(report.totals.sync_effect_bindings_ms)
         << " simulation_ms=" << avg_ms(report.totals.simulation_ms)
         << " packing_ms=" << avg_ms(report.totals.packing_ms)
         << " additive_grouping_ms=" << avg_ms(report.totals.additive_grouping_ms)
         << " alpha_collect_ms=" << avg_ms(report.totals.alpha_collect_ms)
         << " alpha_sort_only_ms=" << avg_ms(report.totals.alpha_sort_only_ms)
         << " alpha_span_ms=" << avg_ms(report.totals.alpha_span_ms)
         << " alpha_sort_ms=" << avg_ms(report.totals.alpha_sort_ms)
         << " distortion_collect_ms=" << avg_ms(report.totals.distortion_collect_ms)
         << " distortion_sort_only_ms=" << avg_ms(report.totals.distortion_sort_only_ms)
         << " distortion_span_ms=" << avg_ms(report.totals.distortion_span_ms)
         << " distortion_sort_ms=" << avg_ms(report.totals.distortion_sort_ms)
         << " draw_submission_ms=" << avg_ms(report.totals.draw_submission_ms)
         << " scene_color_copy=" << (report.totals.scene_color_copy ? "true" : "false")
         << " post_particle_scene_color_copy="
         << (report.totals.post_particle_scene_color_copy ? "true" : "false")
         << " alpha_half_res=" << (report.totals.alpha_half_res ? "true" : "false")
         << " distortion_present=" << (report.totals.distortion_present ? "true" : "false")
         << " gpu_sort_overflow=" << (report.totals.gpu_sort_overflow ? "true" : "false")
         << " gpu_fallback_active=" << (report.totals.gpu_fallback_active ? "true" : "false")
         << " gpu_global_sort_active="
         << (report.totals.gpu_global_sort_active ? "true" : "false")
         << " gpu_grouped_sort_fallback="
         << (report.totals.gpu_grouped_sort_fallback ? "true" : "false");
  return stream.str();
}

}  // namespace karma::rendering


#include <cstdint>

namespace karma::rendering {

/// \ingroup karma_rendering
/// Renderer command counters reported by the active graphics backend.
///
/// Counters are backend lifetime totals unless a backend explicitly documents a
/// different reset policy. They are intended for diagnostics and performance
/// overlays, not gameplay decisions.
struct RendererCommandStats {
  uint32_t set_pipeline_state = 0;
  uint32_t commit_shader_resources = 0;
  uint32_t set_vertex_buffers = 0;
  uint32_t set_index_buffer = 0;
  uint32_t set_render_targets = 0;
  uint32_t set_viewports = 0;
  uint32_t set_scissor_rects = 0;
  uint32_t clear_render_target = 0;
  uint32_t clear_depth_stencil = 0;
  uint32_t draw = 0;
  uint32_t draw_indexed = 0;
  uint32_t draw_indirect = 0;
  uint32_t draw_indexed_indirect = 0;
  uint32_t multi_draw = 0;
  uint32_t multi_draw_indexed = 0;
  uint32_t dispatch_compute = 0;
  uint32_t dispatch_compute_indirect = 0;
  uint32_t draw_mesh = 0;
  uint32_t draw_mesh_indirect = 0;
  uint32_t trace_rays = 0;
  uint32_t trace_rays_indirect = 0;
  uint32_t update_buffer = 0;
  uint32_t copy_buffer = 0;
  uint32_t map_buffer = 0;
  uint32_t update_texture = 0;
  uint32_t copy_texture = 0;
  uint32_t map_texture_subresource = 0;
  uint32_t begin_query = 0;
  uint32_t generate_mips = 0;
  uint32_t resolve_texture_subresource = 0;
  uint32_t total_triangles = 0;
  uint32_t total_lines = 0;
  uint32_t total_points = 0;
};

/// \ingroup karma_rendering
/// CPU-side renderer timing and resource-creation counters for the most recent
/// completed backend frame.
struct RendererFrameTimingStats {
  uint64_t submitted_frames = 0;
  uint64_t completed_frames = 0;
  uint64_t dropped_frames = 0;
  uint32_t render_queue_depth = 0;
  float frame_record_ms = 0.0f;
  float frame_submit_ms = 0.0f;
  float render_thread_wait_ms = 0.0f;
  float render_thread_frame_ms = 0.0f;
  float render_thread_command_wait_ms = 0.0f;
  uint32_t render_layer_count = 0;
  uint32_t render_layer_draws = 0;
  float render_layer_total_ms = 0.0f;
  float target_setup_ms = 0.0f;
  float clear_ms = 0.0f;
  float camera_setup_ms = 0.0f;
  float environment_ms = 0.0f;
  float forward_plus_ms = 0.0f;
  float shadow_ms = 0.0f;
  float terrain_ms = 0.0f;
  float forward_collect_ms = 0.0f;
  float opaque_ms = 0.0f;
  float transparent_ms = 0.0f;
  float particle_resources_ms = 0.0f;
  float particle_pass_ms = 0.0f;
  float line_resources_ms = 0.0f;
  float line_draw_ms = 0.0f;
  float post_process_ms = 0.0f;
  float present_copy_ms = 0.0f;
  float render_ui_ms = 0.0f;
  float swapchain_present_ms = 0.0f;
  float skipped_present_flush_ms = 0.0f;
  float resource_creation_ms = 0.0f;
  float pipeline_creation_ms = 0.0f;
  float resize_ms = 0.0f;
  uint32_t resource_creation_count = 0;
  uint32_t pipeline_creation_count = 0;
  uint32_t resize_events = 0;
  uint32_t skipped_presents = 0;
};

/// \ingroup karma_rendering
/// Forward+ light-culling diagnostics.
struct ForwardPlusStats {
  uint32_t tile_size = 16;
  uint32_t max_lights_per_tile = 128;
  uint32_t max_local_lights = 4096;
  uint32_t tiles_x = 0;
  uint32_t tiles_y = 0;
  uint32_t local_light_count = 0;
  bool active = false;
  bool cpu_fallback = false;
  bool overflow_risk = false;
};

/// \ingroup karma_rendering
/// Instanced-rendering diagnostics for the most recent backend frame.
struct InstancingStats {
  uint32_t submitted_batches = 0;
  uint32_t submitted_instances = 0;
  uint32_t drawn_batches = 0;
  uint32_t drawn_instances = 0;
  uint32_t culled_batches = 0;
  uint32_t draw_calls = 0;
  uint32_t instance_buffer_updates = 0;
  uint32_t gpu_culling_batches = 0;
  uint32_t gpu_culling_dispatches = 0;
  uint32_t gpu_culling_candidate_instances = 0;
  uint32_t gpu_indirect_draws = 0;
  uint32_t lod_bucket_count = 0;
  uint32_t lod_culling_dispatches = 0;
  uint32_t lod_candidate_instances = 0;
  uint32_t lod_indirect_draws = 0;
  uint32_t lod_fallbacks = 0;
  uint64_t instance_upload_bytes = 0;
  float render_system_extraction_ms = 0.0f;
  float forward_state_collection_ms = 0.0f;
  float instance_upload_ms = 0.0f;
};


}  // namespace karma::rendering


#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>


namespace karma::rendering {

/// Integer terrain tile coordinate in the terrain XZ tile grid.
struct TerrainTileCoord {
  int32_t x = 0;
  int32_t z = 0;

  friend bool operator==(const TerrainTileCoord& lhs,
                         const TerrainTileCoord& rhs) = default;
};

/// Terrain resource creation descriptor.
struct TerrainDesc {
  float tile_size = 1000.0f;
  uint32_t tile_resolution = 257u;
  int32_t origin_tile_x = 0;
  int32_t origin_tile_z = 0;
  float height_scale = 120.0f;
  float height_offset = 0.0f;
  uint32_t base_patch_size = 16u;
  float max_tessellation_factor = 16.0f;
  float target_tessellated_edge_size = 12.0f;
  bool cpu_fallback_enabled = true;
};

/// Decoded RGBA8 terrain texture payload.
struct TerrainTextureData {
  uint32_t width = 0u;
  uint32_t height = 0u;
  std::vector<uint8_t> rgba8;

  bool valid() const {
    const std::size_t byte_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    return width > 0u && height > 0u && rgba8.size() == byte_count;
  }
};

/// Shared repeated material layer uploaded to a terrain resource.
struct TerrainMaterialLayerData {
  uint32_t layer = 0u;
  std::string name;
  float uv_scale = 16.0f;
  bool enabled = true;
  TerrainTextureData albedo;
  TerrainTextureData normal;
  TerrainTextureData roughness;

  bool valid() const {
    return enabled && layer < 4u && uv_scale > 0.0f && albedo.valid();
  }
};

/// Optional decoded terrain data map payload carried with a tile.
struct TerrainDataMapTileData {
  std::string name;
  uint32_t width = 0u;
  uint32_t height = 0u;
  std::vector<float> values;

  bool valid() const {
    return width > 0u && height > 0u &&
           values.size() == static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height);
  }
};

/// Decoded tile payload uploaded to the renderer backend.
///
/// `heights` are normalized scalar samples. Backends apply `height_scale` and
/// `height_offset` from `TerrainDesc`. `color_rgba8` is an orthophoto/albedo
/// tile in top-left row order. `control_rgba8`, when provided, packs up to four
/// material-layer weights in RGBA channels.
struct TerrainTileData {
  TerrainTileCoord coord{};
  uint32_t resolution = 0u;
  std::vector<float> heights;
  uint32_t color_width = 0u;
  uint32_t color_height = 0u;
  std::vector<uint8_t> color_rgba8;
  uint32_t control_width = 0u;
  uint32_t control_height = 0u;
  std::vector<uint8_t> control_rgba8;
  std::vector<TerrainDataMapTileData> data_maps;

  bool valid() const {
    const std::size_t sample_count =
        static_cast<std::size_t>(resolution) * static_cast<std::size_t>(resolution);
    const std::size_t color_count =
        static_cast<std::size_t>(color_width) * static_cast<std::size_t>(color_height) * 4u;
    const std::size_t control_count =
        static_cast<std::size_t>(control_width) *
        static_cast<std::size_t>(control_height) * 4u;
    const bool control_valid =
        control_rgba8.empty() ||
        (control_width > 0u &&
         control_height > 0u &&
         control_rgba8.size() == control_count);
    return resolution >= 2u &&
           heights.size() == sample_count &&
           color_width > 0u &&
           color_height > 0u &&
           color_rgba8.size() == color_count &&
           control_valid;
  }
};

/// One terrain tile submission for the current frame.
struct TerrainDrawItem {
  InstanceId instance = kInvalidInstance;
  TerrainId terrain = kInvalidTerrain;
  TerrainTileCoord coord{};
  glm::mat4 transform{1.0f};
  LayerId layer = 0u;
  bool visible = true;
};

/// Backend terrain support flags.
struct TerrainCapabilities {
  bool supported = false;
  bool hardware_tessellation = false;
  bool cpu_fallback = true;
  uint32_t max_tessellation_factor = 64u;
};

/// Per-frame terrain diagnostics from the active backend.
struct TerrainStats {
  uint32_t terrain_count = 0u;
  uint32_t resident_tiles = 0u;
  uint32_t submitted_tiles = 0u;
  uint32_t drawn_tiles = 0u;
  uint32_t culled_tiles = 0u;
  uint32_t upload_count = 0u;
  uint32_t eviction_count = 0u;
  uint32_t tessellated_tiles = 0u;
  uint32_t cpu_fallback_tiles = 0u;
};

}  // namespace karma::rendering


#include <cstdint>
#include <vector>

namespace karma::rendering {

/// \ingroup karma_rendering
/// Renderer texture handle used by UI draw commands.
using UITextureHandle = uint32_t;

/// \ingroup karma_rendering
/// One UI vertex in screen-space pixels.
struct UIVertex {
  float x = 0.0f;
  float y = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
  uint32_t rgba = 0;
};

/// \ingroup karma_rendering
/// Draw command referencing a span of `UIDrawData::indices`.
struct UIDrawCmd {
  uint32_t index_offset = 0;
  uint32_t index_count = 0;
  bool scissor_enabled = false;
  int scissor_x = 0;
  int scissor_y = 0;
  int scissor_w = 0;
  int scissor_h = 0;
  UITextureHandle texture = 0;
};

/// \ingroup karma_rendering
/// Provider-neutral UI draw list consumed by the renderer.
struct UIDrawData {
  std::vector<UIVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<UIDrawCmd> commands;
  bool premultiplied_alpha = false;

  /// Clears vertices, indices, commands, and alpha mode for a new frame.
  void clear() {
    vertices.clear();
    indices.clear();
    commands.clear();
    premultiplied_alpha = false;
  }
};

}  // namespace karma::rendering


#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace karma::platform {
class Window;
}

namespace karma::world {
class DeformationSystem;
}

namespace karma::visual::particles {
class ParticleSystem;
}

namespace karma::rendering {

class RenderSystem;

/// Startup present-mode preference for backends with explicit swapchain present modes.
///
/// `Auto` preserves the existing `vsync` policy: vsync on selects a vblank-paced
/// mode, vsync off selects the backend's low-latency preference. The explicit
/// values request a concrete mode when the platform supports it.
enum class PresentMode {
  Auto,
  Immediate,
  Mailbox,
  Fifo,
  FifoRelaxed,
};

/// Renderer/backend execution ownership.
///
/// `Threaded` records frames on the game thread and executes backend work,
/// including swapchain present, on a dedicated render thread. `Synchronous`
/// preserves the single-threaded execution model and is mainly useful for
/// tests, debugging, and backend bring-up.
enum class RendererExecutionMode {
  Threaded,
  Synchronous,
};

struct GraphicsDeviceCreateInfo {
  bool vsync = false;
  PresentMode present_mode = PresentMode::Auto;
  RendererExecutionMode execution_mode = RendererExecutionMode::Threaded;
};

/// \ingroup karma_rendering
/// High-level renderer facade owned by `EngineApp`.
///
/// `GraphicsDevice` forwards resource creation, scene submission, UI rendering,
/// diagnostics, and frame lifecycle calls to the configured backend. Game code
/// generally reaches it through `GameInterface::graphics`; ordinary scene
/// rendering is driven by `RenderSystem`.
class GraphicsDevice {
 public:
  /// Creates the configured graphics backend for `window`.
  explicit GraphicsDevice(karma::platform::Window& window,
                          const GraphicsDeviceCreateInfo& create_info = {});
  ~GraphicsDevice();

  /// Begins a frame with viewport/timing data.
  void beginFrame(const FrameInfo& frame);
  /// Ends and presents the current frame.
  void endFrame(bool wait_for_completion = false);
  /// Waits until all queued renderer work has completed.
  void waitIdle();
  /// Resizes backend swapchain/framebuffer resources.
  void resize(int width, int height);
  /// Prewarms lazily-created renderer resources expected by startup scenes.
  void prewarmRendererResources(bool include_ui = false);
  /// Persists backend pipeline/render-state cache work when supported.
  void flushRenderStateCache();
  /// Returns the current framebuffer size.
  void getFramebufferSize(int& width, int& height) const;

  /// Uploads CPU mesh data and returns a mesh handle.
  MeshId createMesh(const world::MeshData& mesh);
  /// Replaces mesh data for an existing handle.
  void updateMesh(MeshId mesh, const world::MeshData& data);
  /// Destroys a mesh handle.
  void destroyMesh(MeshId mesh);
  /// Queries cached mesh bounds.
  bool getMeshBounds(MeshId mesh, glm::vec3& center, float& radius) const;
  /// Queries mesh material-slot metadata.
  bool getMeshMaterialSlots(MeshId mesh, std::vector<world::MeshMaterialSlot>& out_slots) const;

  /// Creates a material from resolved material parameters.
  MaterialId createMaterial(const ResolvedMaterialDesc& material);
  /// Creates a material from explicit surface parameters.
  MaterialId createMaterial(const MaterialDesc& material);
  /// Creates a material from an imported asset material index.
  MaterialId createMaterialFromAsset(const std::filesystem::path& path, uint32_t material_index);
  /// Updates material parameters.
  void updateMaterial(MaterialId material, const MaterialDesc& desc);
  /// Destroys a material.
  void destroyMaterial(MaterialId material);
  /// Sets a named float parameter on a material when supported.
  void setMaterialFloat(MaterialId material, std::string_view name, float value);

  /// Creates a texture from descriptor data.
  TextureId createTexture(const TextureDesc& desc);
  /// Returns true when the active backend can create and sample a texture format.
  bool supportsTextureFormat(TextureFormat format) const;
  /// Uploads prepared texture subresources when the backend supports the format.
  bool uploadTexture(TextureId texture, const TextureUploadData& upload);
  /// Destroys a texture.
  void destroyTexture(TextureId texture);

  /// Creates a render target.
  RenderTargetId createRenderTarget(const RenderTargetDesc& desc);
  /// Destroys a render target.
  void destroyRenderTarget(RenderTargetId target);

  /// Creates a streamed terrain resource.
  TerrainId createTerrain(const TerrainDesc& desc);
  /// Destroys a streamed terrain resource.
  void destroyTerrain(TerrainId terrain);
  /// Uploads or replaces one decoded terrain tile.
  void uploadTerrainTile(TerrainId terrain, const TerrainTileData& tile);
  /// Uploads or replaces one shared repeated terrain material layer.
  void uploadTerrainMaterialLayer(TerrainId terrain, const TerrainMaterialLayerData& layer);
  /// Removes all shared repeated material layers from a terrain resource.
  void clearTerrainMaterialLayers(TerrainId terrain);
  /// Evicts one terrain tile from a streamed terrain resource.
  void evictTerrainTile(TerrainId terrain, TerrainTileCoord coord);
  /// Submits one streamed terrain tile draw.
  void submitTerrain(const TerrainDrawItem& item);
  /// Returns active backend terrain capabilities.
  TerrainCapabilities getTerrainCapabilities() const;
  /// Returns active backend terrain diagnostics.
  TerrainStats getTerrainStats() const;

  /// Creates renderer-owned skin/morph deformation resources.
  DeformationId createDeformation(const DeformationDesc& desc);
  /// Updates renderer-owned skin/morph deformation resources.
  void updateDeformation(DeformationId deformation, const DeformationDesc& desc);
  /// Destroys renderer-owned skin/morph deformation resources.
  void destroyDeformation(DeformationId deformation);
  /// Returns active backend deformation diagnostics.
  DeformationStats getDeformationStats() const;

  /// Submits one mesh draw item.
  void submit(const DrawItem& item);
  /// Submits one shared mesh draw with many instances.
  void submitInstanced(const InstancedDrawItem& item);
  /// Submits a compatibility particle batch.
  void submitParticles(ParticleBatch batch);
  /// Submits a packed particle batch.
  void submitPackedParticles(PackedParticleBatch batch);
  /// Submits a renderer-owned particle emitter descriptor.
  void submitParticleEmitter(const ParticleEmitterGpuDesc& emitter);
  /// Submits a renderer-owned particle beam/ribbon descriptor.
  void submitParticleBeam(const ParticleBeamGpuDesc& beam);
  /// Provides particle-system timings/counters to the renderer.
  void setParticleSystemStats(const ParticlePassStats& stats);
  /// Retires a renderer instance id.
  void retireInstance(InstanceId instance);
  /// Renders one layer into a target with resolved post-process settings.
  ///
  /// Normal applications let `RenderSystem` call this after resolving the
  /// active camera profile. Custom render paths must pass the settings for that
  /// specific camera pass; there is no backend-global post-process state API.
  void renderLayer(LayerId layer,
                   RenderTargetId target,
                   const PostProcessSettings& post_process);
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
  /// Sets the framebuffer clear color used when no skybox/background geometry covers a pixel.
  void setClearColor(const math::Color& color);
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
  /// Updates CPU-side instancing timings measured outside the backend.
  void setInstancingCpuTimings(float render_system_extraction_ms,
                               float forward_state_collection_ms = -1.0f);
  /// Returns latest instanced-rendering diagnostics.
  InstancingStats getInstancingStats() const;
  /// Returns latest particle-pass diagnostics.
  ParticlePassStats getParticlePassStats() const;
  /// Returns renderer backend command counters.
  RendererCommandStats getRendererCommandStats() const;
  /// Returns renderer backend frame timings for the most recently completed frame.
  RendererFrameTimingStats getRendererFrameTimingStats() const;
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
  void renderUi(const karma::rendering::UIDrawData& draw_data);

 private:
  friend class RenderSystem;
  friend class karma::world::DeformationSystem;
  friend class karma::visual::particles::ParticleSystem;
  class RenderScheduler;

  /// Registers or replaces a runtime mesh backing a content mesh asset.
  MeshId registerRuntimeMesh(const std::string& key, const world::MeshData& mesh);
  /// Removes a runtime mesh registration.
  void unregisterRuntimeMesh(const std::string& key);
  /// Returns a registered runtime mesh id, or `kInvalidMesh`.
  MeshId findRuntimeMesh(const std::string& key) const;

  std::unique_ptr<RenderScheduler> scheduler_;
  struct RuntimeMeshRegistration {
    MeshId mesh = kInvalidMesh;
    world::MeshData data;
  };
  std::unordered_map<std::string, RuntimeMeshRegistration> runtime_meshes_;
  mutable std::recursive_mutex mutex_;
  int framebuffer_width_ = 0;
  int framebuffer_height_ = 0;
};

using Renderer = GraphicsDevice;

}  // namespace karma::rendering


#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace karma::assets {
class AssetRegistry;
struct AssetPackageHandle;
}  // namespace karma::assets

namespace karma::world {
class Scene;
class World;
}  // namespace karma::world

namespace karma::rendering {

class GraphicsDevice;

/// \ingroup karma_rendering
/// Opaque handle for renderer assets pinned by explicit prewarm.
struct RenderPrewarmHandle {
  uint64_t id = 0u;
  bool valid() const { return id != 0u; }
};

/// \ingroup karma_rendering
/// Extracts ECS render data and submits it to `GraphicsDevice`.
///
/// `RenderSystem` resolves mesh/material keys, maintains shared renderer
/// resources, extracts cameras/lights/environment, resolves camera-selected
/// post-process profiles, submits offscreen camera passes, submits the primary
/// camera pass, and cleans up renderer resources for destroyed entities.
class RenderSystem {
 public:
  /// Binds renderer extraction to the device and normalized runtime assets.
  RenderSystem(GraphicsDevice& device, const assets::AssetRegistry& assets);
  ~RenderSystem();

  RenderSystem(RenderSystem&&) noexcept;
  RenderSystem& operator=(RenderSystem&&) noexcept;

  RenderSystem(const RenderSystem&) = delete;
  RenderSystem& operator=(const RenderSystem&) = delete;

  /// Extracts the world/scene for one frame and submits render data.
  void update(world::World& world, world::Scene& scene, float dt, float interpolation_alpha);
  /// Uploads and pins selected assets until `releasePrewarm` is called.
  RenderPrewarmHandle prewarmAssets(const std::vector<std::string>& mesh_keys,
                                    const std::vector<std::string>& material_keys,
                                    const std::vector<std::string>& texture_keys = {});
  /// Uploads and pins renderer-facing assets from a loaded package handle.
  RenderPrewarmHandle prewarmPackage(const karma::assets::AssetPackageHandle& package);
  /// Releases assets pinned by a previous prewarm call.
  bool releasePrewarm(RenderPrewarmHandle handle);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::rendering
