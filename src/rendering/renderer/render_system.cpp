#include "karma/rendering.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "karma/assets.h"
#include "karma/math.h"
#include "karma/core.h"
#include "render_system/debug_draw.h"
#include "render_system/extractors.h"
#include "karma/components.h"

namespace karma::rendering {

namespace {
using render_system::drawBoxWire;
using render_system::drawCapsuleWire;
using render_system::drawSphereWire;
using render_system::toCameraData;
using render_system::toDirectionalLight;
using render_system::toLightData;
using render_system::toTransform;

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

bool envFlagDisabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") == 0 ||
         std::strcmp(value, "false") == 0 ||
         std::strcmp(value, "FALSE") == 0 ||
         std::strcmp(value, "off") == 0 ||
         std::strcmp(value, "OFF") == 0;
}

bool renderSystemDiagEnabled() {
  static const bool enabled = envFlagEnabled(std::getenv("KARMA_RENDER_SYSTEM_DIAG")) ||
                              envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  return enabled;
}

bool renderSystemDiagEveryFrameEnabled() {
  static const bool enabled =
      envFlagEnabled(std::getenv("KARMA_RENDER_SYSTEM_DIAG_EVERY_FRAME"));
  return enabled;
}

assets::TextureAsset makePreparedTextureAsset(
    const assets::TextureAsset& source,
    const assets::PreparedTextureUpload& prepared) {
  assets::TextureAsset texture{};
  texture.desc = prepared.desc;
  texture.payload_format = assets::TextureAsset::PayloadFormat::PreparedUpload;
  texture.semantic = source.semantic;
  texture.subresources = prepared.upload.subresources;
  texture.bytes = prepared.upload.bytes;
  return texture;
}

void logRenderSystemStage(const bool enabled,
                          const char* name,
                          const core::SteadyClock::time_point start,
                          const core::SteadyClock::time_point end) {
  if (enabled) {
    spdlog::info("RenderSystem stage '{}' took {:.2f} ms",
                 name,
                 core::elapsedMilliseconds(start, end));
  }
}

glm::mat4 toInstanceTransform(const components::MeshInstance& instance) {
  glm::mat4 transform = glm::translate(glm::mat4(1.0f), math::toGlm(instance.position));
  transform *= glm::mat4_cast(math::toGlm(instance.rotation));
  transform = glm::scale(transform, math::toGlm(instance.scale));
  return transform;
}

rendering::InstanceData toRendererInstance(const components::MeshInstance& instance) {
  rendering::InstanceData out{};
  out.transform = toInstanceTransform(instance);
  out.params = glm::vec4(instance.params[0],
                         instance.params[1],
                         instance.params[2],
                         instance.params[3]);
  return out;
}

rendering::PlanarInstanceData toRendererPlanarInstance(
    const components::PlanarMeshInstance& instance) {
  rendering::PlanarInstanceData out{};
  out.position_yaw = glm::vec4(instance.position.x,
                               instance.position.y,
                               instance.position.z,
                               instance.yaw_radians);
  out.scale_pad = glm::vec4(instance.scale.x, instance.scale.y, instance.scale.z, 0.0f);
  out.params = glm::vec4(instance.params[0],
                         instance.params[1],
                         instance.params[2],
                         instance.params[3]);
  return out;
}

glm::mat4 toPlanarInstanceTransform(const components::PlanarMeshInstance& instance) {
  glm::mat4 transform = glm::translate(glm::mat4(1.0f), math::toGlm(instance.position));
  transform = glm::rotate(transform, instance.yaw_radians, glm::vec3(0.0f, 1.0f, 0.0f));
  transform = glm::scale(transform, math::toGlm(instance.scale));
  return transform;
}

float maxTransformScale(const glm::mat4& transform) {
  const float sx = glm::length(glm::vec3(transform[0]));
  const float sy = glm::length(glm::vec3(transform[1]));
  const float sz = glm::length(glm::vec3(transform[2]));
  return std::max(sx, std::max(sy, sz));
}

void mergeSphere(glm::vec3& center,
                 float& radius,
                 bool& valid,
                 const glm::vec3& next_center,
                 float next_radius) {
  if (next_radius <= 0.0f) {
    return;
  }
  if (!valid) {
    center = next_center;
    radius = next_radius;
    valid = true;
    return;
  }
  const glm::vec3 delta = next_center - center;
  const float distance = glm::length(delta);
  if (distance + next_radius <= radius) {
    return;
  }
  if (distance + radius <= next_radius) {
    center = next_center;
    radius = next_radius;
    return;
  }
  if (distance <= 1.0e-5f) {
    radius = std::max(radius, next_radius);
    return;
  }
  const float new_radius = (radius + distance + next_radius) * 0.5f;
  center += delta * ((new_radius - radius) / distance);
  radius = new_radius;
}

template <typename RenderRecordT>
void rebuildCachedInstances(const components::InstancedMeshComponent& instanced,
                            RenderRecordT& record) {
  record.cached_instance_layout = instanced.gpu_layout;
  record.cached_instance_revision = instanced.instance_revision;
  record.cached_instance_dynamic = instanced.dynamic;
  record.cached_instance_bounds_valid = false;
  record.cached_instance_bounds_center = glm::vec3(0.0f);
  record.cached_instance_bounds_radius = 0.0f;
  record.cached_instances.clear();
  record.cached_planar_instances.clear();

  if (instanced.gpu_layout == rendering::InstanceGpuLayout::PositionYawScaleParams) {
    record.cached_planar_instances.reserve(instanced.planar_instances.size());
    for (const auto& instance : instanced.planar_instances) {
      record.cached_planar_instances.push_back(toRendererPlanarInstance(instance));
      if (record.bounds_valid) {
        const glm::mat4 transform = toPlanarInstanceTransform(instance);
        const glm::vec3 center =
            glm::vec3(transform * glm::vec4(record.bounds_center, 1.0f));
        mergeSphere(record.cached_instance_bounds_center,
                    record.cached_instance_bounds_radius,
                    record.cached_instance_bounds_valid,
                    center,
                    record.bounds_radius * maxTransformScale(transform));
      }
    }
    record.cached_instance_count = record.cached_planar_instances.size();
    return;
  }

  record.cached_instances.reserve(instanced.instances.size());
  for (const auto& instance : instanced.instances) {
    rendering::InstanceData renderer_instance = toRendererInstance(instance);
    if (record.bounds_valid) {
      const glm::vec3 center =
          glm::vec3(renderer_instance.transform * glm::vec4(record.bounds_center, 1.0f));
      mergeSphere(record.cached_instance_bounds_center,
                  record.cached_instance_bounds_radius,
                  record.cached_instance_bounds_valid,
                  center,
                  record.bounds_radius * maxTransformScale(renderer_instance.transform));
    }
    record.cached_instances.push_back(renderer_instance);
  }
  record.cached_instance_count = record.cached_instances.size();
}

size_t authoredInstanceCount(const components::InstancedMeshComponent& instanced) {
  switch (instanced.gpu_layout) {
    case rendering::InstanceGpuLayout::Matrix4x4Params:
      return instanced.instances.size();
    case rendering::InstanceGpuLayout::PositionYawScaleParams:
      return instanced.planar_instances.size();
  }
  return 0u;
}

template <typename T>
void appendScalar(std::ostringstream& stream, std::string_view name, const T& value) {
  stream << name << '=' << value << ';';
}

void appendColor(std::ostringstream& stream, std::string_view name, const math::Color& value) {
  stream << name << '=' << value.r << ',' << value.g << ',' << value.b << ',' << value.a << ';';
}

void appendMaterialParameter(std::ostringstream& stream,
                             const rendering::MaterialParameterValue& value) {
  std::visit(
      [&](const auto& typed) {
        using Value = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Value, bool>) {
          stream << (typed ? "true" : "false");
        } else if constexpr (std::is_same_v<Value, rendering::Color>) {
          stream << typed.r << ',' << typed.g << ',' << typed.b << ',' << typed.a;
        } else if constexpr (std::is_same_v<Value, glm::vec2>) {
          stream << typed.x << ',' << typed.y;
        } else if constexpr (std::is_same_v<Value, glm::vec3>) {
          stream << typed.x << ',' << typed.y << ',' << typed.z;
        } else if constexpr (std::is_same_v<Value, glm::vec4>) {
          stream << typed.x << ',' << typed.y << ',' << typed.z << ',' << typed.w;
        } else {
          stream << typed;
        }
      },
      value);
}

std::string materialFingerprint(const rendering::ResolvedMaterialDesc& resolved) {
  std::ostringstream stream;
  stream << "pipeline=" << resolved.pipeline.name << ';'
         << "vs=" << resolved.pipeline.vertex_shader_path.generic_string() << ';'
         << "ps=" << resolved.pipeline.fragment_shader_path.generic_string() << ';'
         << "ve=" << resolved.pipeline.vertex_entry_point << ';'
         << "pe=" << resolved.pipeline.fragment_entry_point << ';';
  std::vector<std::string> defines = resolved.pipeline.defines;
  std::sort(defines.begin(), defines.end());
  for (const std::string& define : defines) {
    stream << "define=" << define << ';';
  }

  const rendering::MaterialDesc& material = resolved.surface;
  appendColor(stream, "base_color", material.base_color);
  appendColor(stream, "emissive_color", material.emissive_color);
  appendScalar(stream, "metallic", material.metallic);
  appendScalar(stream, "roughness", material.roughness);
  appendScalar(stream, "normal_scale", material.normal_scale);
  appendScalar(stream, "occlusion_strength", material.occlusion_strength);
  appendScalar(stream, "emissive_strength", material.emissive_strength);
  appendScalar(stream, "clearcoat", material.clearcoat);
  appendScalar(stream, "clearcoat_roughness", material.clearcoat_roughness);
  appendColor(stream, "sheen_color", material.sheen_color);
  appendScalar(stream, "sheen_roughness", material.sheen_roughness);
  appendScalar(stream, "anisotropy", material.anisotropy);
  appendScalar(stream, "transmission", material.transmission);
  appendScalar(stream, "ior", material.ior);
  appendScalar(stream, "thickness", material.thickness);
  appendScalar(stream, "attenuation_distance", material.attenuation_distance);
  appendColor(stream, "attenuation_color", material.attenuation_color);
  appendScalar(stream, "analytic_sphere_normals", material.analytic_sphere_normals);
  appendScalar(stream, "unlit", material.unlit);
  appendScalar(stream, "alpha_mode", static_cast<uint32_t>(material.alpha_mode));
  appendScalar(stream, "alpha_cutoff", material.alpha_cutoff);
  appendScalar(stream, "alpha_softness", material.alpha_softness);
  appendScalar(stream, "alpha_dither", material.alpha_dither);
  appendScalar(stream, "alpha_to_coverage", material.alpha_to_coverage);
  appendScalar(stream, "transparent", material.transparent);
  appendScalar(stream, "blend_mode", static_cast<uint32_t>(material.blend_mode));
  appendScalar(stream, "depth_test", material.depth_test);
  appendScalar(stream, "depth_write", material.depth_write);
  appendScalar(stream, "wireframe", material.wireframe);
  appendScalar(stream, "double_sided", material.double_sided);

  std::vector<std::string> param_keys;
  param_keys.reserve(resolved.params.size());
  for (const auto& [key, value] : resolved.params) {
    (void)value;
    param_keys.push_back(key);
  }
  std::sort(param_keys.begin(), param_keys.end());
  for (const std::string& key : param_keys) {
    stream << "param:" << key << '=';
    appendMaterialParameter(stream, resolved.params.at(key));
    stream << ';';
  }

  std::vector<std::string> texture_keys;
  texture_keys.reserve(resolved.textures.size());
  for (const auto& [alias, texture_key] : resolved.textures) {
    texture_keys.push_back(alias + "=" + texture_key);
  }
  std::sort(texture_keys.begin(), texture_keys.end());
  for (const std::string& texture : texture_keys) {
    stream << "texture:" << texture << ';';
  }
  stream << "asset=" << resolved.material_asset_path.generic_string()
         << '#' << resolved.material_asset_index << ';';
  return stream.str();
}

}

struct RenderSystem::Impl {
  Impl(GraphicsDevice& device, const assets::AssetRegistry& assets)
      : device_(device), assets_(&assets) {}

  struct InstancedLodRecord {
    float start_distance = 0.0f;
    std::string mesh_asset_key;
    std::vector<world::MeshMaterialSlot> material_slots;
    std::vector<components::MeshMaterialAssignment> component_materials;
    std::vector<std::string> acquired_material_keys;
    std::vector<rendering::DrawMaterialBinding> material_bindings;
    rendering::MeshId mesh = rendering::kInvalidMesh;
    glm::vec3 bounds_center{0.0f};
    float bounds_radius = 0.0f;
    bool bounds_valid = false;
    rendering::InstanceLodRenderMode render_mode = rendering::InstanceLodRenderMode::Mesh;
    bool shadow_visible = false;
  };

  struct RenderRecord {
    std::string mesh_asset_key;
    std::vector<world::MeshMaterialSlot> material_slots;
    std::vector<components::MeshMaterialAssignment> component_materials;
    std::vector<std::string> acquired_material_keys;
    std::vector<rendering::DrawMaterialBinding> material_bindings;
    rendering::MeshId mesh = rendering::kInvalidMesh;
    glm::vec3 bounds_center{0.0f};
    float bounds_radius = 0.0f;
    bool bounds_valid = false;
    rendering::InstanceGpuLayout cached_instance_layout =
        rendering::InstanceGpuLayout::Matrix4x4Params;
    uint64_t cached_instance_revision = UINT64_MAX;
    size_t cached_instance_count = 0;
    bool cached_instance_dynamic = false;
    bool cached_instance_bounds_valid = false;
    glm::vec3 cached_instance_bounds_center{0.0f};
    float cached_instance_bounds_radius = 0.0f;
    std::vector<rendering::InstanceData> cached_instances;
    std::vector<rendering::PlanarInstanceData> cached_planar_instances;
    std::vector<InstancedLodRecord> instanced_lods;
  };

  struct SharedMeshResource {
    rendering::MeshId mesh = rendering::kInvalidMesh;
    uint32_t ref_count = 0;
    glm::vec3 bounds_center{0.0f};
    float bounds_radius = 0.0f;
    bool bounds_valid = false;
    bool owned_by_render_system = false;
  };

  struct SharedMaterialResource {
    rendering::MaterialId material = rendering::kInvalidMaterial;
    uint32_t ref_count = 0;
    std::vector<std::string> texture_asset_keys;
  };

  struct SharedMaterialAlias {
    std::string fingerprint;
    uint32_t ref_count = 0;
  };

  struct SharedTextureResource {
    rendering::TextureId texture = rendering::kInvalidTexture;
    uint32_t ref_count = 0;
  };

  struct PrewarmRecord {
    std::vector<std::string> mesh_asset_keys;
    std::vector<std::string> material_keys;
    std::vector<std::string> texture_keys;
  };

  static uint64_t entityKey(world::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }
  static uint64_t instancedEntityKey(world::Entity entity) {
    return entityKey(entity) | (uint64_t{1} << 63);
  }
  static world::Entity entityFromKey(uint64_t key) {
    world::Entity entity{};
    entity.index = static_cast<uint32_t>(key >> 32);
    entity.generation = static_cast<uint32_t>(key & 0xFFFFFFFFu);
    return entity;
  }
  static world::Entity entityFromInstancedKey(uint64_t key) {
    return entityFromKey(key & ~(uint64_t{1} << 63));
  }

  void update(world::World& world, world::Scene& scene, float dt, float interpolation_alpha);
  RenderPrewarmHandle prewarmAssets(const std::vector<std::string>& mesh_keys,
                                    const std::vector<std::string>& material_keys,
                                    const std::vector<std::string>& texture_keys);
  RenderPrewarmHandle prewarmPackage(const karma::assets::AssetPackageHandle& package);
  bool releasePrewarm(RenderPrewarmHandle handle);

  void releaseRecord(uint64_t key, RenderRecord& record);
  void cleanupStaleRecords(world::World& world);
  void cleanupStaleInstancedRecords(world::World& world);
  void releaseMeshBinding(RenderRecord& record);
  void releaseMaterialBinding(RenderRecord& record);
  void releaseInstancedLodBindings(RenderRecord& record);
  bool syncInstancedLodBindings(const components::InstancedMeshComponent& instanced,
                                RenderRecord& record);
  void bindMesh(const components::MeshComponent& mesh, RenderRecord& record);
  void bindMesh(const std::string& mesh_asset_key, RenderRecord& record);
  void bindMaterial(const components::MeshComponent& mesh, RenderRecord& record);
  void bindMaterial(const std::vector<components::MeshMaterialAssignment>& materials,
                    RenderRecord& record);
  void acquireSharedMesh(const std::string& mesh_asset_key, RenderRecord& record);
  void releaseSharedMesh(const std::string& mesh_asset_key);
  rendering::MaterialId acquireSharedMaterial(const std::string& material_key);
  void releaseSharedMaterial(const std::string& material_key);
  rendering::TextureId acquireSharedTexture(const std::string& texture_key);
  std::size_t acquireSharedTexturesBatched(const std::vector<std::string>& texture_keys,
                                           std::vector<std::string>& acquired_texture_keys);
  void releaseSharedTexture(const std::string& texture_key);
  rendering::FrameGraphDesc resolveFrameGraphDesc(
      const std::string& key,
      std::unordered_set<std::string>& referenced_texture_keys);
  void releaseInactiveFrameGraphTextures(
      const std::unordered_set<std::string>& referenced_texture_keys);

  GraphicsDevice& device_;
  const assets::AssetRegistry* assets_ = nullptr;
  std::unordered_map<uint64_t, RenderRecord> records_;
  std::unordered_map<uint64_t, RenderRecord> instanced_records_;
  std::unordered_map<std::string, SharedMeshResource> shared_meshes_;
  std::unordered_map<std::string, SharedMaterialResource> shared_materials_;
  std::unordered_map<std::string, SharedMaterialAlias> shared_material_aliases_;
  std::unordered_map<std::string, SharedTextureResource> shared_textures_;
  std::unordered_set<std::string> active_frame_graph_texture_keys_;
  std::unordered_map<uint64_t, PrewarmRecord> prewarm_records_;
  std::unordered_map<std::string, rendering::RenderTargetId> render_targets_by_key_;
  std::unordered_map<std::string, bool> warned_missing_mesh_asset_keys_;
  std::unordered_map<std::string, bool> warned_missing_material_keys_;
  std::unordered_map<std::string, bool> warned_missing_environment_map_keys_;
  uint64_t last_asset_registry_version_ = 0;
  std::string last_env_path_;
  float last_env_intensity_ = -1.0f;
  bool last_env_draw_skybox_ = false;
  bool warned_no_camera_ = false;
  uint64_t next_prewarm_id_ = 1u;
};

RenderSystem::RenderSystem(GraphicsDevice& device, const assets::AssetRegistry& assets)
    : impl_(std::make_unique<Impl>(device, assets)) {}

RenderSystem::~RenderSystem() = default;

RenderSystem::RenderSystem(RenderSystem&&) noexcept = default;

RenderSystem& RenderSystem::operator=(RenderSystem&&) noexcept = default;

void RenderSystem::update(world::World& world,
                          world::Scene& scene,
                          float dt,
                          float interpolation_alpha) {
  impl_->update(world, scene, dt, interpolation_alpha);
}

RenderPrewarmHandle RenderSystem::prewarmAssets(
    const std::vector<std::string>& mesh_keys,
    const std::vector<std::string>& material_keys,
    const std::vector<std::string>& texture_keys) {
  return impl_->prewarmAssets(mesh_keys, material_keys, texture_keys);
}

RenderPrewarmHandle RenderSystem::prewarmPackage(
    const karma::assets::AssetPackageHandle& package) {
  return impl_->prewarmPackage(package);
}

bool RenderSystem::releasePrewarm(RenderPrewarmHandle handle) {
  return impl_->releasePrewarm(handle);
}

void RenderSystem::Impl::releaseRecord(uint64_t key, RenderRecord& record) {
  device_.retireInstance(static_cast<InstanceId>(key));
  releaseInstancedLodBindings(record);
  releaseMaterialBinding(record);
  releaseMeshBinding(record);
}

void RenderSystem::Impl::cleanupStaleRecords(world::World& world) {
  for (auto it = records_.begin(); it != records_.end();) {
    const world::Entity entity = entityFromKey(it->first);
    const bool stale = !world.isAlive(entity) ||
                       !world.has<components::MeshComponent>(entity) ||
                       !world.has<components::TransformComponent>(entity);
    if (!stale) {
      ++it;
      continue;
    }
    releaseRecord(it->first, it->second);
    it = records_.erase(it);
  }
}

void RenderSystem::Impl::cleanupStaleInstancedRecords(world::World& world) {
  for (auto it = instanced_records_.begin(); it != instanced_records_.end();) {
    const world::Entity entity = entityFromInstancedKey(it->first);
    const bool stale = !world.isAlive(entity) ||
                       !world.has<components::InstancedMeshComponent>(entity);
    if (!stale) {
      ++it;
      continue;
    }
    releaseRecord(it->first, it->second);
    it = instanced_records_.erase(it);
  }
}

void RenderSystem::Impl::releaseMeshBinding(RenderRecord& record) {
  releaseSharedMesh(record.mesh_asset_key);
  record.mesh_asset_key.clear();
  record.material_slots.clear();

  record.mesh = rendering::kInvalidMesh;
  record.bounds_center = glm::vec3(0.0f);
  record.bounds_radius = 0.0f;
  record.bounds_valid = false;
}

void RenderSystem::Impl::releaseMaterialBinding(RenderRecord& record) {
  for (const std::string& material_key : record.acquired_material_keys) {
    releaseSharedMaterial(material_key);
  }
  record.acquired_material_keys.clear();
  record.material_bindings.clear();
}

void RenderSystem::Impl::releaseInstancedLodBindings(RenderRecord& record) {
  for (auto& lod : record.instanced_lods) {
    for (const std::string& material_key : lod.acquired_material_keys) {
      releaseSharedMaterial(material_key);
    }
    lod.acquired_material_keys.clear();
    lod.material_bindings.clear();
    releaseSharedMesh(lod.mesh_asset_key);
    lod.mesh_asset_key.clear();
    lod.mesh = rendering::kInvalidMesh;
    lod.bounds_center = glm::vec3(0.0f);
    lod.bounds_radius = 0.0f;
    lod.bounds_valid = false;
  }
  record.instanced_lods.clear();
}

bool RenderSystem::Impl::syncInstancedLodBindings(
    const components::InstancedMeshComponent& instanced,
    RenderRecord& record) {
  std::vector<components::InstancedMeshLodLevel> desired;
  desired.reserve(std::min(instanced.lods.size(), components::kMaxInstancedMeshLodLevels));
  for (const auto& lod : instanced.lods) {
    if (desired.size() >= components::kMaxInstancedMeshLodLevels) {
      break;
    }
    if (lod.mesh_asset_key.empty()) {
      continue;
    }
    desired.push_back(lod);
  }
  std::sort(desired.begin(), desired.end(), [](const auto& a, const auto& b) {
    return a.start_distance < b.start_distance;
  });

  bool needs_rebind = desired.size() != record.instanced_lods.size();
  if (!needs_rebind) {
    for (size_t i = 0; i < desired.size(); ++i) {
      const auto& wanted = desired[i];
      const auto& current = record.instanced_lods[i];
      if (wanted.start_distance != current.start_distance ||
          wanted.mesh_asset_key != current.mesh_asset_key ||
          wanted.materials != current.component_materials ||
          wanted.render_mode != current.render_mode ||
          wanted.shadow_visible != current.shadow_visible) {
        needs_rebind = true;
        break;
      }
    }
  }
  if (!needs_rebind) {
    return false;
  }

  releaseInstancedLodBindings(record);
  record.instanced_lods.reserve(desired.size());
  for (const auto& wanted : desired) {
    InstancedLodRecord lod_record{};
    lod_record.start_distance = std::max(wanted.start_distance, 0.0f);
    lod_record.render_mode = wanted.render_mode;
    lod_record.shadow_visible = wanted.shadow_visible;

    RenderRecord mesh_record{};
    acquireSharedMesh(wanted.mesh_asset_key, mesh_record);
    if (mesh_record.mesh == rendering::kInvalidMesh) {
      continue;
    }
    lod_record.mesh_asset_key = wanted.mesh_asset_key;
    lod_record.mesh = mesh_record.mesh;
    lod_record.bounds_center = mesh_record.bounds_center;
    lod_record.bounds_radius = mesh_record.bounds_radius;
    lod_record.bounds_valid = mesh_record.bounds_valid;
    lod_record.material_slots = std::move(mesh_record.material_slots);

    if (lod_record.mesh != rendering::kInvalidMesh) {
      device_.getMeshMaterialSlots(lod_record.mesh, lod_record.material_slots);
    }

    RenderRecord material_record{};
    material_record.material_slots = lod_record.material_slots;
    bindMaterial(wanted.materials, material_record);
    lod_record.component_materials = wanted.materials;
    lod_record.acquired_material_keys = std::move(material_record.acquired_material_keys);
    lod_record.material_bindings = std::move(material_record.material_bindings);
    record.instanced_lods.push_back(std::move(lod_record));
  }
  return true;
}

void RenderSystem::Impl::bindMesh(const components::MeshComponent& mesh, RenderRecord& record) {
  bindMesh(mesh.mesh_asset_key, record);
}

void RenderSystem::Impl::bindMesh(const std::string& mesh_asset_key, RenderRecord& record) {
  record.mesh_asset_key = mesh_asset_key;
  acquireSharedMesh(mesh_asset_key, record);
  record.material_slots.clear();
  if (record.mesh != rendering::kInvalidMesh) {
    device_.getMeshMaterialSlots(record.mesh, record.material_slots);
  }
}

void RenderSystem::Impl::bindMaterial(const components::MeshComponent& mesh, RenderRecord& record) {
  bindMaterial(mesh.materials, record);
}

void RenderSystem::Impl::bindMaterial(const std::vector<components::MeshMaterialAssignment>& materials,
                                RenderRecord& record) {
  record.component_materials = materials;
  record.material_bindings.clear();
  record.acquired_material_keys.clear();

  uint32_t slot_count = static_cast<uint32_t>(record.material_slots.size());
  for (const auto& binding : materials) {
    slot_count = std::max(slot_count, binding.slot + 1u);
  }

  auto assigned_material_for_slot = [&](uint32_t slot) -> const std::string* {
    for (const auto& binding : materials) {
      if (binding.slot == slot && !binding.material_key.empty()) {
        return &binding.material_key;
      }
    }
    return nullptr;
  };

  for (uint32_t slot = 0; slot < slot_count; ++slot) {
    const std::string* material_key = assigned_material_for_slot(slot);
    const std::string* fallback_key =
        slot < record.material_slots.size() &&
                !record.material_slots[slot].default_material_key.empty()
            ? &record.material_slots[slot].default_material_key
            : nullptr;

    rendering::MaterialId material = rendering::kInvalidMaterial;
    const std::string* acquired_key = nullptr;
    if (material_key != nullptr) {
      material = acquireSharedMaterial(*material_key);
      if (material != rendering::kInvalidMaterial) {
        acquired_key = material_key;
      }
    }
    if (material == rendering::kInvalidMaterial && fallback_key != nullptr &&
        (material_key == nullptr || *fallback_key != *material_key)) {
      material = acquireSharedMaterial(*fallback_key);
      if (material != rendering::kInvalidMaterial) {
        acquired_key = fallback_key;
      }
    }
    if (material == rendering::kInvalidMaterial) {
      continue;
    }
    record.acquired_material_keys.push_back(*acquired_key);
    record.material_bindings.push_back(rendering::DrawMaterialBinding{
        .slot = slot,
        .material = material,
    });
  }
}

void RenderSystem::Impl::acquireSharedMesh(const std::string& mesh_asset_key, RenderRecord& record) {
  record.mesh = rendering::kInvalidMesh;
  record.bounds_center = glm::vec3(0.0f);
  record.bounds_radius = 0.0f;
  record.bounds_valid = false;
  if (mesh_asset_key.empty()) {
    return;
  }

  auto shared_it = shared_meshes_.find(mesh_asset_key);
  if (shared_it == shared_meshes_.end()) {
    SharedMeshResource shared{};
    shared.mesh = device_.findRuntimeMesh(mesh_asset_key);
    shared.owned_by_render_system = false;
    if (shared.mesh == rendering::kInvalidMesh) {
      const world::MeshData* mesh_asset =
          assets_ != nullptr ? assets_->findMeshAsset(mesh_asset_key) : nullptr;
      if (mesh_asset == nullptr) {
        if (!warned_missing_mesh_asset_keys_.contains(mesh_asset_key)) {
          spdlog::error("Karma: mesh asset key '{}' was not registered", mesh_asset_key);
          warned_missing_mesh_asset_keys_.emplace(mesh_asset_key, true);
        }
        return;
      }
      shared.mesh = device_.registerRuntimeMesh(mesh_asset_key, *mesh_asset);
      shared.owned_by_render_system = shared.mesh != rendering::kInvalidMesh;
    }
    if (shared.mesh != rendering::kInvalidMesh) {
      shared.bounds_valid =
          device_.getMeshBounds(shared.mesh, shared.bounds_center, shared.bounds_radius);
    }
    shared.ref_count = 1;
    shared_it = shared_meshes_.emplace(mesh_asset_key, std::move(shared)).first;
  } else {
    shared_it->second.ref_count += 1;
  }

  record.mesh = shared_it->second.mesh;
  record.bounds_center = shared_it->second.bounds_center;
  record.bounds_radius = shared_it->second.bounds_radius;
  record.bounds_valid = shared_it->second.bounds_valid;
}

void RenderSystem::Impl::releaseSharedMesh(const std::string& mesh_asset_key) {
  if (mesh_asset_key.empty()) {
    return;
  }
  auto shared_it = shared_meshes_.find(mesh_asset_key);
  if (shared_it == shared_meshes_.end()) {
    return;
  }
  if (shared_it->second.ref_count > 0) {
    shared_it->second.ref_count -= 1;
  }
  if (shared_it->second.ref_count == 0) {
    if (shared_it->second.owned_by_render_system &&
        shared_it->second.mesh != rendering::kInvalidMesh) {
      device_.unregisterRuntimeMesh(mesh_asset_key);
    }
    shared_meshes_.erase(shared_it);
  }
}

rendering::TextureId RenderSystem::Impl::acquireSharedTexture(const std::string& texture_key) {
  if (texture_key.empty() || assets_ == nullptr) {
    return rendering::kInvalidTexture;
  }

  const bool diag_enabled = renderSystemDiagEnabled();
  const auto cache_start = core::SteadyClock::now();
  auto shared_it = shared_textures_.find(texture_key);
  if (shared_it != shared_textures_.end()) {
    shared_it->second.ref_count += 1u;
    if (diag_enabled) {
      spdlog::info("RenderSystem texture cache hit texture='{}' took {:.2f} ms",
                   texture_key,
                   core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
    return shared_it->second.texture;
  }

  const auto lookup_start = core::SteadyClock::now();
  const assets::TextureAsset* texture_asset = assets_->findTextureAsset(texture_key);
  const auto lookup_end = core::SteadyClock::now();
  if (texture_asset == nullptr ||
      texture_asset->desc.width <= 0 ||
      texture_asset->desc.height <= 0) {
    if (diag_enabled) {
      spdlog::info(
          "RenderSystem texture cache miss texture='{}' failed lookup_ms={:.2f} total_ms={:.2f}",
          texture_key,
          core::elapsedMilliseconds(lookup_start, lookup_end),
          core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
    return rendering::kInvalidTexture;
  }

  const auto capabilities_start = core::SteadyClock::now();
  const assets::TextureRuntimeCapabilities capabilities{
      .bc7_unorm = device_.supportsTextureFormat(rendering::TextureFormat::BC7_RGBA_UNORM),
      .bc7_srgb = device_.supportsTextureFormat(rendering::TextureFormat::BC7_RGBA_UNORM_SRGB),
  };
  const auto capabilities_end = core::SteadyClock::now();
  const auto prepare_start = core::SteadyClock::now();
  auto prepared = assets::prepareTextureUpload(*texture_asset, capabilities);
  const auto prepare_end = core::SteadyClock::now();
  if (!prepared.has_value()) {
    if (diag_enabled) {
      spdlog::info(
          "RenderSystem texture cache miss texture='{}' failed lookup_ms={:.2f} "
          "capabilities_ms={:.2f} prepare_ms={:.2f} total_ms={:.2f}",
          texture_key,
          core::elapsedMilliseconds(lookup_start, lookup_end),
          core::elapsedMilliseconds(capabilities_start, capabilities_end),
          core::elapsedMilliseconds(prepare_start, prepare_end),
          core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
    return rendering::kInvalidTexture;
  }

  SharedTextureResource shared{};
  const auto create_start = core::SteadyClock::now();
  shared.texture = device_.createTexture(prepared->desc);
  const auto create_end = core::SteadyClock::now();
  if (shared.texture == rendering::kInvalidTexture) {
    if (diag_enabled) {
      spdlog::info(
          "RenderSystem texture cache miss texture='{}' failed lookup_ms={:.2f} "
          "capabilities_ms={:.2f} prepare_ms={:.2f} create_ms={:.2f} total_ms={:.2f}",
          texture_key,
          core::elapsedMilliseconds(lookup_start, lookup_end),
          core::elapsedMilliseconds(capabilities_start, capabilities_end),
          core::elapsedMilliseconds(prepare_start, prepare_end),
          core::elapsedMilliseconds(create_start, create_end),
          core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
    return rendering::kInvalidTexture;
  }
  const auto upload_start = core::SteadyClock::now();
  const bool uploaded = device_.uploadTexture(shared.texture, prepared->upload);
  const auto upload_end = core::SteadyClock::now();
  if (!uploaded) {
    device_.destroyTexture(shared.texture);
    if (diag_enabled) {
      spdlog::info(
          "RenderSystem texture cache miss texture='{}' failed lookup_ms={:.2f} "
          "capabilities_ms={:.2f} prepare_ms={:.2f} create_ms={:.2f} upload_ms={:.2f} "
          "total_ms={:.2f}",
          texture_key,
          core::elapsedMilliseconds(lookup_start, lookup_end),
          core::elapsedMilliseconds(capabilities_start, capabilities_end),
          core::elapsedMilliseconds(prepare_start, prepare_end),
          core::elapsedMilliseconds(create_start, create_end),
          core::elapsedMilliseconds(upload_start, upload_end),
          core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
    return rendering::kInvalidTexture;
  }
  shared.ref_count = 1u;
  shared_it = shared_textures_.emplace(texture_key, std::move(shared)).first;
  if (diag_enabled) {
    spdlog::info(
        "RenderSystem texture cache miss texture='{}' lookup_ms={:.2f} "
        "capabilities_ms={:.2f} prepare_ms={:.2f} create_ms={:.2f} upload_ms={:.2f} "
        "total_ms={:.2f}",
        texture_key,
        core::elapsedMilliseconds(lookup_start, lookup_end),
        core::elapsedMilliseconds(capabilities_start, capabilities_end),
        core::elapsedMilliseconds(prepare_start, prepare_end),
        core::elapsedMilliseconds(create_start, create_end),
        core::elapsedMilliseconds(upload_start, upload_end),
        core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
  }
  return shared_it->second.texture;
}

std::size_t RenderSystem::Impl::acquireSharedTexturesBatched(
    const std::vector<std::string>& texture_keys,
    std::vector<std::string>& acquired_texture_keys) {
  if (texture_keys.empty() || assets_ == nullptr) {
    return 0u;
  }

  constexpr std::size_t kTextureBatchSize = 16u;
  const bool diag_enabled = renderSystemDiagEnabled();
  const auto total_start = core::SteadyClock::now();
  const auto capabilities_start = core::SteadyClock::now();
  const assets::TextureRuntimeCapabilities capabilities{
      .bc7_unorm = device_.supportsTextureFormat(rendering::TextureFormat::BC7_RGBA_UNORM),
      .bc7_srgb = device_.supportsTextureFormat(rendering::TextureFormat::BC7_RGBA_UNORM_SRGB),
  };
  const auto capabilities_end = core::SteadyClock::now();
  assets::AssetCacheConfig prepared_cache_config = assets::AssetCacheConfig::fromEnvironment();
  prepared_cache_config.flush = false;
  const bool prepared_cache_enabled =
      prepared_cache_config.enabled &&
      !prepared_cache_config.root.empty() &&
      !envFlagDisabled(std::getenv("KARMA_RENDER_TEXTURE_PREPARED_CACHE"));
  if (diag_enabled) {
    spdlog::info(
        "RenderSystem texture batch capabilities requested={} bc7_unorm={} bc7_srgb={} "
        "prepared_cache={} took {:.2f} ms",
        texture_keys.size(),
        capabilities.bc7_unorm ? "true" : "false",
        capabilities.bc7_srgb ? "true" : "false",
        prepared_cache_enabled ? "on" : "off",
        core::elapsedMilliseconds(capabilities_start, capabilities_end));
  }

  struct PreparedTextureResult {
    std::optional<assets::PreparedTextureUpload> prepared;
    double cache_read_ms = 0.0;
    double prepare_ms = 0.0;
    double cache_write_ms = 0.0;
    bool prepared_cache_hit = false;
    bool prepared_cache_write = false;
  };

  struct PendingTexture {
    std::string key;
    double lookup_ms = 0.0;
    std::future<PreparedTextureResult> prepare;
  };

  struct ReadyTexture {
    std::string key;
    double lookup_ms = 0.0;
    double cache_read_ms = 0.0;
    double prepare_ms = 0.0;
    double cache_write_ms = 0.0;
    bool prepared_cache_hit = false;
    bool prepared_cache_write = false;
  };

  auto prepare_texture =
      [capabilities,
       prepared_cache_config,
       prepared_cache_enabled](const assets::TextureAsset* texture_asset) {
    PreparedTextureResult result{};
    if (texture_asset == nullptr) {
      return result;
    }

    const std::string prepared_cache_key =
        prepared_cache_enabled
            ? assets::preparedTextureUploadCacheKey(*texture_asset, capabilities)
            : std::string{};
    if (prepared_cache_enabled && !prepared_cache_key.empty()) {
      const auto read_start = core::SteadyClock::now();
      assets::AssetCache cache(prepared_cache_config);
      std::string diagnostic;
      std::optional<assets::TextureAsset> cached =
          cache.readTexture(prepared_cache_key, &diagnostic);
      const auto read_end = core::SteadyClock::now();
      result.cache_read_ms = core::elapsedMilliseconds(read_start, read_end);
      if (cached.has_value()) {
        const auto prepare_start = core::SteadyClock::now();
        result.prepared = assets::prepareTextureUpload(*cached, capabilities);
        const auto prepare_end = core::SteadyClock::now();
        result.prepare_ms = core::elapsedMilliseconds(prepare_start, prepare_end);
        if (result.prepared.has_value()) {
          result.prepared_cache_hit = true;
          return result;
        }
      }
    }

    const auto prepare_start = core::SteadyClock::now();
    result.prepared = assets::prepareTextureUpload(*texture_asset, capabilities);
    const auto prepare_end = core::SteadyClock::now();
    result.prepare_ms = core::elapsedMilliseconds(prepare_start, prepare_end);

    if (prepared_cache_enabled &&
        !prepared_cache_key.empty() &&
        result.prepared.has_value()) {
      assets::TextureAsset cached_texture =
          makePreparedTextureAsset(*texture_asset, *result.prepared);
      cached_texture.content_hash = "prepared:" + prepared_cache_key;
      const auto write_start = core::SteadyClock::now();
      assets::AssetCache cache(prepared_cache_config);
      result.prepared_cache_write =
          cache.writeTextureNoIndex(prepared_cache_key, cached_texture);
      const auto write_end = core::SteadyClock::now();
      result.cache_write_ms = core::elapsedMilliseconds(write_start, write_end);
    }
    return result;
  };

  std::size_t acquired_count = 0u;
  std::vector<PendingTexture> pending;
  std::vector<rendering::TextureUploadBatchRequest> requests;
  pending.reserve(kTextureBatchSize);
  requests.reserve(kTextureBatchSize);

  auto flush_pending = [&]() {
    if (pending.empty()) {
      return;
    }

    std::vector<ReadyTexture> ready;
    ready.reserve(pending.size());
    requests.clear();
    requests.reserve(pending.size());
    for (PendingTexture& texture : pending) {
      PreparedTextureResult prepared = texture.prepare.get();
      if (!prepared.prepared.has_value()) {
        if (diag_enabled) {
          spdlog::info(
              "RenderSystem texture cache miss texture='{}' failed lookup_ms={:.2f} "
              "prepared_cache={} cache_read_ms={:.2f} prepare_ms={:.2f} "
              "cache_write_ms={:.2f} total_ms={:.2f} mode=batch",
              texture.key,
              texture.lookup_ms,
              prepared.prepared_cache_hit ? "hit" : "miss",
              prepared.cache_read_ms,
              prepared.prepare_ms,
              prepared.cache_write_ms,
              texture.lookup_ms + prepared.cache_read_ms + prepared.prepare_ms +
                  prepared.cache_write_ms);
        }
        continue;
      }

      ready.push_back(ReadyTexture{
          .key = texture.key,
          .lookup_ms = texture.lookup_ms,
          .cache_read_ms = prepared.cache_read_ms,
          .prepare_ms = prepared.prepare_ms,
          .cache_write_ms = prepared.cache_write_ms,
          .prepared_cache_hit = prepared.prepared_cache_hit,
          .prepared_cache_write = prepared.prepared_cache_write,
      });
      requests.push_back(rendering::TextureUploadBatchRequest{
          .desc = prepared.prepared->desc,
          .upload = std::move(prepared.prepared->upload),
      });
    }
    if (requests.empty()) {
      pending.clear();
      return;
    }

    const auto batch_start = core::SteadyClock::now();
    std::vector<rendering::TextureUploadBatchResult> results =
        device_.createAndUploadTextures(std::move(requests));
    const auto batch_end = core::SteadyClock::now();
    if (diag_enabled) {
      spdlog::info("RenderSystem texture batch upload count={} took {:.2f} ms",
                   ready.size(),
                   core::elapsedMilliseconds(batch_start, batch_end));
    }

    const std::size_t result_count = std::min(results.size(), ready.size());
    for (std::size_t index = 0u; index < result_count; ++index) {
      const ReadyTexture& texture = ready[index];
      const rendering::TextureUploadBatchResult& result = results[index];
      if (result.uploaded && result.texture != rendering::kInvalidTexture) {
        SharedTextureResource shared{};
        shared.texture = result.texture;
        shared.ref_count = 1u;
        shared_textures_.emplace(texture.key, std::move(shared));
        acquired_texture_keys.push_back(texture.key);
        ++acquired_count;
      }
      if (diag_enabled) {
        spdlog::info(
            "RenderSystem texture cache miss texture='{}' lookup_ms={:.2f} "
            "capabilities_ms=0.00 prepared_cache={} cache_read_ms={:.2f} "
            "prepare_ms={:.2f} cache_write_ms={:.2f} create_ms={:.2f} "
            "upload_ms={:.2f} total_ms={:.2f} mode=batch",
            texture.key,
            texture.lookup_ms,
            texture.prepared_cache_hit ? "hit" : "miss",
            texture.cache_read_ms,
            texture.prepare_ms,
            texture.cache_write_ms,
            result.create_ms,
            result.upload_ms,
            texture.lookup_ms + texture.cache_read_ms + texture.prepare_ms +
                texture.cache_write_ms + result.create_ms + result.upload_ms);
      }
    }

    for (std::size_t index = result_count; index < ready.size(); ++index) {
      if (diag_enabled) {
        const ReadyTexture& texture = ready[index];
        spdlog::info(
            "RenderSystem texture cache miss texture='{}' failed lookup_ms={:.2f} "
            "capabilities_ms=0.00 prepared_cache={} cache_read_ms={:.2f} "
            "prepare_ms={:.2f} cache_write_ms={:.2f} total_ms={:.2f} mode=batch",
            texture.key,
            texture.lookup_ms,
            texture.prepared_cache_hit ? "hit" : "miss",
            texture.cache_read_ms,
            texture.prepare_ms,
            texture.cache_write_ms,
            texture.lookup_ms + texture.cache_read_ms + texture.prepare_ms +
                texture.cache_write_ms);
      }
    }

    pending.clear();
    requests.clear();
    requests.reserve(kTextureBatchSize);
  };

  for (const std::string& texture_key : texture_keys) {
    if (texture_key.empty()) {
      continue;
    }
    const auto cache_start = core::SteadyClock::now();
    auto shared_it = shared_textures_.find(texture_key);
    if (shared_it != shared_textures_.end()) {
      shared_it->second.ref_count += 1u;
      acquired_texture_keys.push_back(texture_key);
      ++acquired_count;
      if (diag_enabled) {
        spdlog::info("RenderSystem texture cache hit texture='{}' took {:.2f} ms",
                     texture_key,
                     core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
      }
      continue;
    }

    const auto lookup_start = core::SteadyClock::now();
    const assets::TextureAsset* texture_asset = assets_->findTextureAsset(texture_key);
    const auto lookup_end = core::SteadyClock::now();
    if (texture_asset == nullptr ||
        texture_asset->desc.width <= 0 ||
        texture_asset->desc.height <= 0) {
      if (diag_enabled) {
        spdlog::info(
            "RenderSystem texture cache miss texture='{}' failed lookup_ms={:.2f} total_ms={:.2f}",
            texture_key,
            core::elapsedMilliseconds(lookup_start, lookup_end),
            core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
      }
      continue;
    }

    pending.push_back(PendingTexture{
        .key = texture_key,
        .lookup_ms = core::elapsedMilliseconds(lookup_start, lookup_end),
        .prepare = std::async(std::launch::async, prepare_texture, texture_asset),
    });
    if (pending.size() >= kTextureBatchSize) {
      flush_pending();
    }
  }

  flush_pending();
  if (diag_enabled) {
    spdlog::info("RenderSystem texture batch acquire requested={} acquired={} took {:.2f} ms",
                 texture_keys.size(),
                 acquired_count,
                 core::elapsedMilliseconds(total_start, core::SteadyClock::now()));
  }
  return acquired_count;
}

void RenderSystem::Impl::releaseSharedTexture(const std::string& texture_key) {
  if (texture_key.empty()) {
    return;
  }
  auto shared_it = shared_textures_.find(texture_key);
  if (shared_it == shared_textures_.end()) {
    return;
  }
  if (shared_it->second.ref_count > 0u) {
    shared_it->second.ref_count -= 1u;
  }
  if (shared_it->second.ref_count == 0u) {
    if (shared_it->second.texture != rendering::kInvalidTexture) {
      device_.destroyTexture(shared_it->second.texture);
    }
    shared_textures_.erase(shared_it);
  }
}

rendering::FrameGraphDesc RenderSystem::Impl::resolveFrameGraphDesc(
    const std::string& key,
    std::unordered_set<std::string>& referenced_texture_keys) {
  rendering::FrameGraphDesc graph =
      assets_ != nullptr ? assets_->resolveFrameGraph(key)
                         : rendering::defaultFrameGraphDesc();
  if (assets_ == nullptr) {
    return graph;
  }

  std::unordered_set<std::string> shader_pass_keys;
  for (const rendering::FrameGraphPassDesc& pass : graph.passes) {
    if (!pass.enabled ||
        pass.kind != rendering::FrameGraphPassKind::Shader ||
        pass.shader_pass_key.empty() ||
        !shader_pass_keys.insert(pass.shader_pass_key).second) {
      continue;
    }
    const rendering::ShaderPassAssetDesc* shader_pass =
        assets_->findShaderPass(pass.shader_pass_key);
    if (shader_pass == nullptr) {
      continue;
    }

    rendering::ShaderPassAssetDesc resolved = *shader_pass;
    for (const auto& [alias, texture_key] : resolved.textures) {
      if (texture_key.empty()) {
        continue;
      }
      referenced_texture_keys.insert(texture_key);
      if (active_frame_graph_texture_keys_.find(texture_key) ==
          active_frame_graph_texture_keys_.end()) {
        const rendering::TextureId acquired = acquireSharedTexture(texture_key);
        if (acquired != rendering::kInvalidTexture) {
          active_frame_graph_texture_keys_.insert(texture_key);
        }
      }
      auto shared_it = shared_textures_.find(texture_key);
      if (shared_it != shared_textures_.end() &&
          shared_it->second.texture != rendering::kInvalidTexture) {
        resolved.texture_handles[alias] = shared_it->second.texture;
      }
    }
    graph.shader_pass_assets.push_back(std::move(resolved));
  }
  return graph;
}

void RenderSystem::Impl::releaseInactiveFrameGraphTextures(
    const std::unordered_set<std::string>& referenced_texture_keys) {
  for (auto it = active_frame_graph_texture_keys_.begin();
       it != active_frame_graph_texture_keys_.end();) {
    if (referenced_texture_keys.find(*it) != referenced_texture_keys.end()) {
      ++it;
      continue;
    }
    const std::string texture_key = *it;
    it = active_frame_graph_texture_keys_.erase(it);
    releaseSharedTexture(texture_key);
  }
}

rendering::MaterialId RenderSystem::Impl::acquireSharedMaterial(const std::string& material_key) {
  if (material_key.empty() || assets_ == nullptr) {
    return rendering::kInvalidMaterial;
  }

  const bool diag_enabled = renderSystemDiagEnabled();
  const auto cache_start = core::SteadyClock::now();
  auto alias_it = shared_material_aliases_.find(material_key);
  if (alias_it != shared_material_aliases_.end()) {
    auto shared_it = shared_materials_.find(alias_it->second.fingerprint);
    if (shared_it != shared_materials_.end()) {
      alias_it->second.ref_count += 1u;
      shared_it->second.ref_count += 1u;
      if (diag_enabled) {
        spdlog::info("RenderSystem material cache hit material='{}' took {:.2f} ms",
                     material_key,
                     core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
      }
      return shared_it->second.material;
    }
    shared_material_aliases_.erase(alias_it);
  }

  const auto resolve_start = core::SteadyClock::now();
  std::optional<rendering::ResolvedMaterialDesc> resolved;
  if (assets_ != nullptr) {
    resolved = assets_->resolveMaterial(material_key);
  }
  const auto resolve_end = core::SteadyClock::now();
  if (!resolved.has_value()) {
    if (!warned_missing_material_keys_.contains(material_key)) {
      spdlog::warn("Karma: material key '{}' was not registered; using mesh slot default",
                   material_key);
      warned_missing_material_keys_.emplace(material_key, true);
    }
    if (diag_enabled) {
      spdlog::info(
          "RenderSystem material cache miss material='{}' failed resolve_ms={:.2f} "
          "total_ms={:.2f}",
          material_key,
          core::elapsedMilliseconds(resolve_start, resolve_end),
          core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
    return rendering::kInvalidMaterial;
  }

  const auto fingerprint_start = core::SteadyClock::now();
  const std::string fingerprint = materialFingerprint(*resolved);
  const auto fingerprint_end = core::SteadyClock::now();
  auto shared_it = shared_materials_.find(fingerprint);
  if (shared_it == shared_materials_.end()) {

    std::vector<std::string> acquired_texture_keys;
    acquired_texture_keys.reserve(resolved->textures.size());
    const auto texture_acquire_start = core::SteadyClock::now();
    for (const auto& [alias, texture_key] : resolved->textures) {
      const rendering::TextureId texture = acquireSharedTexture(texture_key);
      if (texture == rendering::kInvalidTexture) {
        continue;
      }
      resolved->texture_handles[alias] = texture;
      acquired_texture_keys.push_back(texture_key);
    }
    const auto texture_acquire_end = core::SteadyClock::now();

    SharedMaterialResource shared{};
    const auto create_start = core::SteadyClock::now();
    shared.material = device_.createMaterial(*resolved);
    const auto create_end = core::SteadyClock::now();
    if (shared.material == rendering::kInvalidMaterial) {
      for (const std::string& texture_key : acquired_texture_keys) {
        releaseSharedTexture(texture_key);
      }
      if (diag_enabled) {
        spdlog::info(
            "RenderSystem material cache miss material='{}' failed resolve_ms={:.2f} "
            "fingerprint_ms={:.2f} texture_acquire_ms={:.2f} textures_requested={} "
            "textures_acquired={} create_ms={:.2f} total_ms={:.2f}",
            material_key,
            core::elapsedMilliseconds(resolve_start, resolve_end),
            core::elapsedMilliseconds(fingerprint_start, fingerprint_end),
            core::elapsedMilliseconds(texture_acquire_start, texture_acquire_end),
            resolved->textures.size(),
            acquired_texture_keys.size(),
            core::elapsedMilliseconds(create_start, create_end),
            core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
      }
      return rendering::kInvalidMaterial;
    }
    shared.ref_count = 1;
    shared.texture_asset_keys = std::move(acquired_texture_keys);
    shared_it = shared_materials_.emplace(fingerprint, std::move(shared)).first;
    if (diag_enabled) {
      spdlog::info(
          "RenderSystem material cache miss detail material='{}' resolve_ms={:.2f} "
          "fingerprint_ms={:.2f} texture_acquire_ms={:.2f} textures_requested={} "
          "textures_acquired={} create_ms={:.2f} total_ms={:.2f}",
          material_key,
          core::elapsedMilliseconds(resolve_start, resolve_end),
          core::elapsedMilliseconds(fingerprint_start, fingerprint_end),
          core::elapsedMilliseconds(texture_acquire_start, texture_acquire_end),
          resolved->textures.size(),
          shared_it->second.texture_asset_keys.size(),
          core::elapsedMilliseconds(create_start, create_end),
          core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
      spdlog::info("RenderSystem material cache miss material='{}' took {:.2f} ms",
                   material_key,
                   core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
  } else {
    shared_it->second.ref_count += 1u;
    if (diag_enabled) {
      spdlog::info("RenderSystem material cache hit material='{}' took {:.2f} ms",
                   material_key,
                   core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
  }

  shared_material_aliases_[material_key] = SharedMaterialAlias{
      .fingerprint = fingerprint,
      .ref_count = 1u,
  };
  return shared_it->second.material;
}

void RenderSystem::Impl::releaseSharedMaterial(const std::string& material_key) {
  if (material_key.empty()) {
    return;
  }

  auto alias_it = shared_material_aliases_.find(material_key);
  if (alias_it == shared_material_aliases_.end()) {
    return;
  }
  const std::string fingerprint = alias_it->second.fingerprint;
  if (alias_it->second.ref_count > 0u) {
    alias_it->second.ref_count -= 1u;
  }
  if (alias_it->second.ref_count == 0u) {
    shared_material_aliases_.erase(alias_it);
  }

  auto shared_it = shared_materials_.find(fingerprint);
  if (shared_it == shared_materials_.end()) {
    return;
  }
  if (shared_it->second.ref_count > 0) {
    shared_it->second.ref_count -= 1;
  }
  if (shared_it->second.ref_count == 0) {
    if (shared_it->second.material != rendering::kInvalidMaterial) {
      device_.destroyMaterial(shared_it->second.material);
    }
    for (const std::string& texture_key : shared_it->second.texture_asset_keys) {
      releaseSharedTexture(texture_key);
    }
    shared_materials_.erase(shared_it);
  }
}

RenderPrewarmHandle RenderSystem::Impl::prewarmAssets(
    const std::vector<std::string>& mesh_keys,
    const std::vector<std::string>& material_keys,
    const std::vector<std::string>& texture_keys) {
  const bool diag_enabled = renderSystemDiagEnabled();
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  PrewarmRecord record{};
  record.mesh_asset_keys.reserve(mesh_keys.size());
  record.material_keys.reserve(material_keys.size());
  record.texture_keys.reserve(texture_keys.size());

  for (const std::string& mesh_key : mesh_keys) {
    RenderRecord mesh_record{};
    acquireSharedMesh(mesh_key, mesh_record);
    if (mesh_record.mesh != rendering::kInvalidMesh) {
      record.mesh_asset_keys.push_back(mesh_key);
    }
  }
  logRenderSystemStage(diag_enabled, "prewarm mesh acquire", stage_start, core::SteadyClock::now());
  if (diag_enabled) {
    spdlog::info("RenderSystem prewarm mesh acquire requested={} acquired={}",
                 mesh_keys.size(),
                 record.mesh_asset_keys.size());
  }

  stage_start = core::SteadyClock::now();
  std::vector<std::string> prewarm_texture_keys;
  prewarm_texture_keys.reserve(texture_keys.size() + material_keys.size());
  std::unordered_set<std::string> queued_texture_keys;
  queued_texture_keys.reserve(texture_keys.size() + material_keys.size());
  auto append_texture_key = [&](const std::string& texture_key) {
    if (!texture_key.empty() && queued_texture_keys.insert(texture_key).second) {
      prewarm_texture_keys.push_back(texture_key);
    }
  };
  for (const std::string& texture_key : texture_keys) {
    append_texture_key(texture_key);
  }
  if (assets_ != nullptr) {
    for (const std::string& material_key : material_keys) {
      if (material_key.empty()) {
        continue;
      }
      std::optional<rendering::ResolvedMaterialDesc> resolved =
          assets_->resolveMaterial(material_key);
      if (!resolved.has_value()) {
        continue;
      }
      for (const auto& [alias, texture_key] : resolved->textures) {
        (void)alias;
        append_texture_key(texture_key);
      }
    }
  }
  logRenderSystemStage(diag_enabled,
                       "prewarm texture key collect",
                       stage_start,
                       core::SteadyClock::now());
  if (diag_enabled) {
    spdlog::info("RenderSystem prewarm texture key collect package_textures={} material_keys={} unique_textures={}",
                 texture_keys.size(),
                 material_keys.size(),
                 prewarm_texture_keys.size());
  }

  stage_start = core::SteadyClock::now();
  acquireSharedTexturesBatched(prewarm_texture_keys, record.texture_keys);
  logRenderSystemStage(diag_enabled, "prewarm texture acquire", stage_start, core::SteadyClock::now());
  if (diag_enabled) {
    spdlog::info("RenderSystem prewarm texture acquire requested={} acquired={}",
                 prewarm_texture_keys.size(),
                 record.texture_keys.size());
  }

  stage_start = core::SteadyClock::now();
  for (const std::string& material_key : material_keys) {
    if (acquireSharedMaterial(material_key) != rendering::kInvalidMaterial) {
      record.material_keys.push_back(material_key);
    }
  }
  logRenderSystemStage(diag_enabled, "prewarm material acquire", stage_start, core::SteadyClock::now());
  if (diag_enabled) {
    spdlog::info("RenderSystem prewarm material acquire requested={} acquired={}",
                 material_keys.size(),
                 record.material_keys.size());
  }

  if (record.mesh_asset_keys.empty() &&
      record.material_keys.empty() &&
      record.texture_keys.empty()) {
    logRenderSystemStage(diag_enabled, "prewarm total empty", total_start, core::SteadyClock::now());
    return {};
  }

  stage_start = core::SteadyClock::now();
  const RenderPrewarmHandle handle{.id = next_prewarm_id_++};
  prewarm_records_.emplace(handle.id, std::move(record));
  logRenderSystemStage(diag_enabled, "prewarm record store", stage_start, core::SteadyClock::now());
  logRenderSystemStage(diag_enabled, "prewarm total", total_start, core::SteadyClock::now());
  return handle;
}

RenderPrewarmHandle RenderSystem::Impl::prewarmPackage(
    const karma::assets::AssetPackageHandle& package) {
  std::vector<std::string> mesh_keys;
  std::vector<std::string> material_keys;
  std::vector<std::string> texture_keys;
  mesh_keys.reserve(package.assets.size());
  material_keys.reserve(package.assets.size());
  texture_keys.reserve(package.assets.size());
  for (const auto& asset : package.assets) {
    if (asset.type == "mesh") {
      mesh_keys.push_back(asset.key);
    } else if (asset.type == "material") {
      material_keys.push_back(asset.key);
    } else if (asset.type == "texture" || asset.type == "texture_rgba8") {
      texture_keys.push_back(asset.key);
    }
  }
  return prewarmAssets(mesh_keys, material_keys, texture_keys);
}

bool RenderSystem::Impl::releasePrewarm(RenderPrewarmHandle handle) {
  if (!handle.valid()) {
    return false;
  }
  auto it = prewarm_records_.find(handle.id);
  if (it == prewarm_records_.end()) {
    return false;
  }
  for (const std::string& material_key : it->second.material_keys) {
    releaseSharedMaterial(material_key);
  }
  for (const std::string& texture_key : it->second.texture_keys) {
    releaseSharedTexture(texture_key);
  }
  for (const std::string& mesh_key : it->second.mesh_asset_keys) {
    releaseSharedMesh(mesh_key);
  }
  prewarm_records_.erase(it);
  return true;
}

void RenderSystem::Impl::update(world::World& world, world::Scene& /*scene*/, float /*dt*/,
                          float interpolation_alpha) {
  static size_t diag_update_count = 0;
  const bool diag_requested = renderSystemDiagEnabled();
  const bool diag_enabled = diag_requested &&
                            (renderSystemDiagEveryFrameEnabled() || diag_update_count == 0);
  if (diag_requested) {
    ++diag_update_count;
  }
  const auto update_start = core::SteadyClock::now();
  auto section_start = update_start;
  auto section_end = update_start;

  if (assets_ != nullptr && assets_->version() != last_asset_registry_version_) {
    for (const std::string& texture_key : active_frame_graph_texture_keys_) {
      releaseSharedTexture(texture_key);
    }
    active_frame_graph_texture_keys_.clear();
    for (auto& [key, record] : records_) {
      (void)key;
      components::MeshComponent mesh_binding{};
      mesh_binding.mesh_asset_key = record.mesh_asset_key;
      mesh_binding.materials = record.component_materials;
      releaseMaterialBinding(record);
      releaseMeshBinding(record);
      bindMesh(mesh_binding, record);
      bindMaterial(mesh_binding, record);
    }
    for (auto& [key, record] : instanced_records_) {
      (void)key;
      const std::string mesh_asset_key = record.mesh_asset_key;
      const std::vector<components::MeshMaterialAssignment> materials = record.component_materials;
      releaseInstancedLodBindings(record);
      releaseMaterialBinding(record);
      releaseMeshBinding(record);
      bindMesh(mesh_asset_key, record);
      bindMaterial(materials, record);
    }
    last_asset_registry_version_ = assets_->version();
  }
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "material cache refresh", section_start, section_end);
  section_start = section_end;

  static bool logged_start = false;
  if (!logged_start) {
    logged_start = true;
  }
  bool has_camera = false;
  rendering::CameraData primary_camera{};
  rendering::FrameGraphDesc primary_frame_graph{};
  struct OffscreenPass {
    rendering::CameraData camera;
    rendering::RenderTargetId target = rendering::kDefaultRenderTarget;
    rendering::FrameGraphDesc frame_graph;
  };
  static thread_local std::vector<OffscreenPass> offscreen_passes;
  static thread_local std::unordered_set<std::string> active_render_target_keys;
  static thread_local std::unordered_set<std::string> active_frame_graph_texture_keys;
  offscreen_passes.clear();
  active_render_target_keys.clear();
  active_frame_graph_texture_keys.clear();
  world.forEach<components::CameraComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    const auto& camera = world.get<components::CameraComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    rendering::CameraData cam = toCameraData(camera, transform, interpolation_alpha);
    rendering::FrameGraphDesc frame_graph =
        resolveFrameGraphDesc(camera.frame_graph_key, active_frame_graph_texture_keys);
    const bool select_as_primary = !has_camera && camera.is_primary;

    if (camera.render_to_texture) {
      rendering::RenderTargetId target_id = camera.render_target;
      if (target_id == rendering::kDefaultRenderTarget && !camera.render_target_key.empty()) {
        active_render_target_keys.insert(camera.render_target_key);
        auto target_it = render_targets_by_key_.find(camera.render_target_key);
        if (target_it == render_targets_by_key_.end()) {
          rendering::RenderTargetDesc target_desc{};
          target_desc.width = 512;
          target_desc.height = 512;
          target_desc.depth = true;
          target_desc.stencil = false;
          target_id = device_.createRenderTarget(target_desc);
          if (target_id != rendering::kDefaultRenderTarget) {
            render_targets_by_key_[camera.render_target_key] = target_id;
          }
        } else {
          target_id = target_it->second;
        }
      }
      if (target_id != rendering::kDefaultRenderTarget) {
        offscreen_passes.push_back(OffscreenPass{
            .camera = select_as_primary ? cam : std::move(cam),
            .target = target_id,
            .frame_graph = select_as_primary ? frame_graph : std::move(frame_graph),
        });
      }
    }

    if (select_as_primary) {
      primary_camera = std::move(cam);
      primary_frame_graph = std::move(frame_graph);
      has_camera = true;
    }
    return true;
  });

  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "camera collection", section_start, section_end);
  section_start = section_end;

  releaseInactiveFrameGraphTextures(active_frame_graph_texture_keys);

  for (auto it = render_targets_by_key_.begin(); it != render_targets_by_key_.end();) {
    if (active_render_target_keys.find(it->first) != active_render_target_keys.end()) {
      ++it;
      continue;
    }
    if (it->second != rendering::kDefaultRenderTarget) {
      device_.destroyRenderTarget(it->second);
    }
    it = render_targets_by_key_.erase(it);
  }
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "render target cleanup", section_start, section_end);
  section_start = section_end;

  if (!has_camera) {
    if (!warned_no_camera_) {
      warned_no_camera_ = true;
    }
    device_.setCameraActive(false);
    static thread_local std::unordered_set<std::string> no_camera_graph_texture_keys;
    no_camera_graph_texture_keys.clear();
    device_.renderLayer(0,
                        rendering::kDefaultRenderTarget,
                        resolveFrameGraphDesc({}, no_camera_graph_texture_keys));
    releaseInactiveFrameGraphTextures(no_camera_graph_texture_keys);
    logRenderSystemStage(diag_enabled, "total", update_start, core::SteadyClock::now());
    return;
  }
  warned_no_camera_ = false;
  device_.setCamera(primary_camera);
  device_.setCameraActive(true);

  rendering::DirectionalLightData light{};
  light.intensity = 0.0f;
  static thread_local std::vector<rendering::LightData> lights;
  lights.clear();
  if (lights.capacity() < 16u) {
    lights.reserve(16u);
  }
  bool has_light = false;
  static bool warned_missing_light_transform = false;
  if (!warned_missing_light_transform) {
    world.forEach<components::LightComponent>([&](const world::Entity entity) {
      if (!world.has<components::TransformComponent>(entity)) {
        warned_missing_light_transform = true;
        return false;
      }
      return true;
    });
  }
  world.forEach<components::LightComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    const auto& light_component = world.get<components::LightComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    bool visible = true;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = world.get<components::VisibilityComponent>(entity).visible;
    }
    if (!visible || light_component.intensity <= 0.0f) {
      return true;
    }
    if (light_component.type != components::LightComponent::Type::Directional) {
      if (light_component.range <= 0.0f) {
        return true;
      }
    }
    lights.push_back(toLightData(light_component, transform, interpolation_alpha));
    if (!has_light && light_component.type == components::LightComponent::Type::Directional) {
      light = toDirectionalLight(light_component, transform, interpolation_alpha);
      has_light = true;
    }
    return true;
  });
  device_.setDirectionalLight(light);
  device_.setLights(lights);
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "lights", section_start, section_end);
  section_start = section_end;

  bool env_found = false;
  world.forEach<components::EnvironmentComponent>([&](const world::Entity entity) {
    const auto& env = world.get<components::EnvironmentComponent>(entity);
    if (!env.enabled) {
      return true;
    }
    std::filesystem::path environment_path;
    if (!env.environment_map_asset_key.empty()) {
      const assets::EnvironmentMapAsset* asset =
          assets_ != nullptr ? assets_->findEnvironmentMap(env.environment_map_asset_key) : nullptr;
      if (asset == nullptr) {
        if (!warned_missing_environment_map_keys_.contains(env.environment_map_asset_key)) {
          spdlog::error("Karma: environment map asset key '{}' was not registered",
                        env.environment_map_asset_key);
          warned_missing_environment_map_keys_.emplace(env.environment_map_asset_key, true);
        }
        return true;
      }
      environment_path = asset->path;
    }
    if (environment_path.string() != last_env_path_ ||
        env.intensity != last_env_intensity_ ||
        env.draw_skybox != last_env_draw_skybox_) {
      device_.setEnvironmentMap(environment_path, env.intensity, env.draw_skybox);
      last_env_path_ = environment_path.string();
      last_env_intensity_ = env.intensity;
      last_env_draw_skybox_ = env.draw_skybox;
    }
    env_found = true;
    return false;
  });
  if (!env_found &&
      (!last_env_path_.empty() || last_env_intensity_ >= 0.0f || last_env_draw_skybox_)) {
    device_.setEnvironmentMap({}, 0.0f, false);
    last_env_path_.clear();
    last_env_intensity_ = -1.0f;
    last_env_draw_skybox_ = false;
  }
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "environment", section_start, section_end);
  section_start = section_end;

  size_t mesh_entity_count = 0;
  size_t new_render_record_count = 0;
  world.forEach<components::MeshComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    ++mesh_entity_count;
    const auto& mesh = world.get<components::MeshComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);

    bool visible = mesh.visible;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }

    const uint64_t key = entityKey(entity);
    auto it = records_.find(key);
    if (it == records_.end()) {
      RenderRecord record;
      const auto bind_mesh_start = core::SteadyClock::now();
      bindMesh(mesh, record);
      const auto bind_mesh_end = core::SteadyClock::now();
      if (diag_enabled) {
        spdlog::info("RenderSystem new record entity={}:{} mesh='{}' bindMesh took {:.2f} ms",
                     entity.index,
                     entity.generation,
                     mesh.mesh_asset_key.empty() ? "<empty>" : mesh.mesh_asset_key,
                     core::elapsedMilliseconds(bind_mesh_start, bind_mesh_end));
      }

      const auto bind_material_start = bind_mesh_end;
      bindMaterial(mesh, record);
      const auto bind_material_end = core::SteadyClock::now();
      if (diag_enabled) {
        spdlog::info(
            "RenderSystem new record entity={}:{} material_slots={} bindMaterial took {:.2f} ms",
            entity.index,
            entity.generation,
            mesh.materials.size(),
            core::elapsedMilliseconds(bind_material_start, bind_material_end));
      }
      it = records_.emplace(key, std::move(record)).first;
      ++new_render_record_count;
    } else {
      const bool mesh_binding_changed = it->second.mesh_asset_key != mesh.mesh_asset_key;

      if (mesh_binding_changed) {
        releaseMaterialBinding(it->second);
        releaseMeshBinding(it->second);
        bindMesh(mesh, it->second);
        bindMaterial(mesh, it->second);
      } else {
        const bool material_binding_changed = it->second.component_materials != mesh.materials;
        if (material_binding_changed) {
          releaseMaterialBinding(it->second);
          bindMaterial(mesh, it->second);
        }
      }
    }

    glm::mat4 world_matrix = toTransform(transform, interpolation_alpha);
    const components::DeformableMeshComponent* deformable_mesh = nullptr;
    if (world.has<components::DeformableMeshComponent>(entity)) {
      const auto& deformation = world.get<components::DeformableMeshComponent>(entity);
      deformable_mesh = &deformation;
      if (deformation.override_render_transform) {
        world_matrix = glm::mat4(1.0f);
        if (deformation.render_transform_entity.isValid() &&
            world.isAlive(deformation.render_transform_entity) &&
            world.has<components::TransformComponent>(deformation.render_transform_entity)) {
          world_matrix =
              toTransform(world.get<components::TransformComponent>(deformation.render_transform_entity),
                          interpolation_alpha);
        }
      }
    }
    DrawItem item{};
    item.instance = static_cast<InstanceId>(key);
    item.mesh = it->second.mesh;
    item.materials = it->second.material_bindings;
    if (world.has<components::RenderTagsComponent>(entity)) {
      item.render_tags = world.get<components::RenderTagsComponent>(entity).tags;
    }
    item.transform = world_matrix;
    item.layer = 0;
    item.visible = visible;
    item.shadow_visible = visible && mesh.shadow_visible;
    if (deformable_mesh != nullptr &&
        deformable_mesh->enabled &&
        deformable_mesh->path == components::DeformationPath::Gpu &&
        deformable_mesh->deformation != rendering::kInvalidDeformation) {
      item.deformation = deformable_mesh->deformation;
    }
    device_.submit(std::move(item));
  });
  section_end = core::SteadyClock::now();
  if (diag_enabled) {
    spdlog::info("RenderSystem stage 'mesh submit' took {:.2f} ms (meshes={} new_records={})",
                 core::elapsedMilliseconds(section_start, section_end),
                 mesh_entity_count,
                 new_render_record_count);
  }
  section_start = section_end;

  size_t instanced_entity_count = 0;
  size_t instanced_instance_count = 0;
  size_t new_instanced_record_count = 0;
  size_t rebuilt_instanced_payload_count = 0;
  world.forEach<components::InstancedMeshComponent>([&](const world::Entity entity) {
    ++instanced_entity_count;
    const auto& instanced = world.get<components::InstancedMeshComponent>(entity);

    bool visible = instanced.visible;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }

    const uint64_t key = instancedEntityKey(entity);
    auto it = instanced_records_.find(key);
    bool binding_changed = false;
    if (it == instanced_records_.end()) {
      RenderRecord record;
      const auto bind_mesh_start = core::SteadyClock::now();
      bindMesh(instanced.mesh_asset_key, record);
      const auto bind_mesh_end = core::SteadyClock::now();
      if (diag_enabled) {
        spdlog::info("RenderSystem new instanced record entity={}:{} mesh='{}' bindMesh took {:.2f} ms",
                     entity.index,
                     entity.generation,
                     instanced.mesh_asset_key.empty() ? "<empty>" : instanced.mesh_asset_key,
                     core::elapsedMilliseconds(bind_mesh_start, bind_mesh_end));
      }

      const auto bind_material_start = bind_mesh_end;
      bindMaterial(instanced.materials, record);
      const auto bind_material_end = core::SteadyClock::now();
      if (diag_enabled) {
        spdlog::info(
            "RenderSystem new instanced record entity={}:{} material_slots={} bindMaterial took {:.2f} ms",
            entity.index,
            entity.generation,
            instanced.materials.size(),
            core::elapsedMilliseconds(bind_material_start, bind_material_end));
      }
      it = instanced_records_.emplace(key, std::move(record)).first;
      ++new_instanced_record_count;
      binding_changed = true;
    } else {
      const bool mesh_binding_changed = it->second.mesh_asset_key != instanced.mesh_asset_key;

      if (mesh_binding_changed) {
        releaseMaterialBinding(it->second);
        releaseMeshBinding(it->second);
        bindMesh(instanced.mesh_asset_key, it->second);
        bindMaterial(instanced.materials, it->second);
        binding_changed = true;
      } else {
        const bool material_binding_changed =
            it->second.component_materials != instanced.materials;
        if (material_binding_changed) {
          releaseMaterialBinding(it->second);
          bindMaterial(instanced.materials, it->second);
          binding_changed = true;
        }
      }
    }
    const bool lod_binding_changed = syncInstancedLodBindings(instanced, it->second);

    const size_t instance_count = authoredInstanceCount(instanced);
    const bool payload_changed =
        instanced.dynamic ||
        binding_changed ||
        lod_binding_changed ||
        it->second.cached_instance_layout != instanced.gpu_layout ||
        it->second.cached_instance_revision != instanced.instance_revision ||
        it->second.cached_instance_count != instance_count;
    if (payload_changed) {
      rebuildCachedInstances(instanced, it->second);
      ++rebuilt_instanced_payload_count;
    }

    InstancedDrawItem item{};
    item.instance = static_cast<InstanceId>(key);
    item.mesh = it->second.mesh;
    item.materials = it->second.material_bindings;
    if (world.has<components::RenderTagsComponent>(entity)) {
      item.render_tags = world.get<components::RenderTagsComponent>(entity).tags;
    }
    item.lods.reserve(it->second.instanced_lods.size());
    for (const auto& lod : it->second.instanced_lods) {
      item.lods.push_back(rendering::InstancedLodDrawDesc{
          .start_distance = lod.start_distance,
          .mesh = lod.mesh,
          .materials = lod.material_bindings,
          .render_mode = lod.render_mode,
          .bounds_center = lod.bounds_center,
          .bounds_radius = lod.bounds_radius,
          .bounds_valid = lod.bounds_valid,
          .shadow_visible = lod.shadow_visible,
      });
    }
    item.gpu_layout = instanced.gpu_layout;
    item.instances = it->second.cached_instances;
    item.planar_instances = it->second.cached_planar_instances;
    item.payload_changed = payload_changed;
    item.revision = instanced.instance_revision;
    item.bounds_center = it->second.cached_instance_bounds_center;
    item.bounds_radius = it->second.cached_instance_bounds_radius;
    item.bounds_valid = it->second.cached_instance_bounds_valid;
    item.layer = 0;
    item.dynamic = instanced.dynamic;
    item.visible = visible;
    item.shadow_visible = visible && instanced.shadow_visible;
    instanced_instance_count += item.instanceCount();
    device_.submitInstanced(std::move(item));
  });
  section_end = core::SteadyClock::now();
  device_.setInstancingCpuTimings(
      static_cast<float>(core::elapsedMilliseconds(section_start, section_end)));
  if (diag_enabled) {
    spdlog::info(
        "RenderSystem stage 'instanced mesh submit' took {:.2f} ms (batches={} instances={} new_records={} rebuilt_payloads={})",
        core::elapsedMilliseconds(section_start, section_end),
        instanced_entity_count,
        instanced_instance_count,
        new_instanced_record_count,
        rebuilt_instanced_payload_count);
  }
  section_start = section_end;

  cleanupStaleRecords(world);
  cleanupStaleInstancedRecords(world);
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "stale record cleanup", section_start, section_end);
  section_start = section_end;

  const math::Color debug_color{0.1f, 1.0f, 0.1f, 1.0f};
  world.forEach<components::TransformComponent, components::ColliderComponent>(
      [&](const world::Entity entity) {
    const auto& collider = world.get<components::ColliderComponent>(entity);
    if (!collider.debug_draw) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (const auto* box = std::get_if<components::BoxColliderShape>(&collider.shape)) {
      drawBoxWire(device_, transform, box->center, box->half_extents, debug_color,
                  interpolation_alpha);
      return;
    }
    if (const auto* sphere = std::get_if<components::SphereColliderShape>(&collider.shape)) {
      drawSphereWire(device_, transform, sphere->center, sphere->radius, debug_color,
                     interpolation_alpha);
      return;
    }
    if (const auto* capsule = std::get_if<components::CapsuleColliderShape>(&collider.shape)) {
      drawCapsuleWire(device_, transform, capsule->center, capsule->radius, capsule->height,
                      debug_color, interpolation_alpha);
      return;
    }
    if (!std::holds_alternative<components::MeshColliderShape>(collider.shape) ||
        !world.has<components::MeshComponent>(entity)) {
      return;
    }
    const uint64_t key = entityKey(entity);
    auto record_it = records_.find(key);
    if (record_it == records_.end() || !record_it->second.bounds_valid) {
      return;
    }
    drawSphereWire(device_, transform,
                   {record_it->second.bounds_center.x, record_it->second.bounds_center.y,
                    record_it->second.bounds_center.z},
                   record_it->second.bounds_radius, debug_color, interpolation_alpha);
  });
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "debug collider draw", section_start, section_end);
  section_start = section_end;

  for (const auto& pass : offscreen_passes) {
    device_.setCamera(pass.camera);
    device_.setCameraActive(true);
    device_.renderLayer(0, pass.target, pass.frame_graph);
  }
  device_.setCamera(primary_camera);
  device_.setCameraActive(true);
  device_.renderLayer(0, rendering::kDefaultRenderTarget, primary_frame_graph);
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "offscreen passes", section_start, section_end);
  logRenderSystemStage(diag_enabled, "total", update_start, section_end);
}

}  // namespace karma::rendering
