#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace karma::renderer_backend {

namespace {

struct alignas(16) InstanceTransformData {
  float col0[4];
  float col1[4];
  float col2[4];
  float col3[4];
};

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

float maxTransformScale(const glm::mat4& transform) {
  const float sx = glm::length(glm::vec3(transform[0]));
  const float sy = glm::length(glm::vec3(transform[1]));
  const float sz = glm::length(glm::vec3(transform[2]));
  return std::max(sx, std::max(sy, sz));
}

glm::vec4 transformBoundingSphere(const glm::mat4& world,
                                  const glm::vec3& local_center,
                                  float local_radius) {
  if (local_radius <= 0.0f) {
    return glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
  }
  const glm::vec3 center = glm::vec3(world * glm::vec4(local_center, 1.0f));
  const float radius = local_radius * maxTransformScale(world);
  return glm::vec4(center, radius);
}

bool sphereIntersectsClipVolume(const glm::mat4& clip_from_world,
                                const glm::vec4& sphere,
                                bool is_gl_ndc) {
  if (sphere.w <= 0.0f) {
    return true;
  }
  const glm::vec4 clip = clip_from_world * glm::vec4(sphere.x, sphere.y, sphere.z, 1.0f);
  if (std::abs(clip.w) <= 1e-5f) {
    return true;
  }
  const float sx = glm::length(glm::vec3(clip_from_world[0]));
  const float sy = glm::length(glm::vec3(clip_from_world[1]));
  const float sz = glm::length(glm::vec3(clip_from_world[2]));
  const float r = sphere.w * std::max(sx, std::max(sy, sz));

  if (clip.x < -clip.w - r || clip.x > clip.w + r) {
    return false;
  }
  if (clip.y < -clip.w - r || clip.y > clip.w + r) {
    return false;
  }
  if (is_gl_ndc) {
    if (clip.z < -clip.w - r || clip.z > clip.w + r) {
      return false;
    }
  } else {
    if (clip.z < -r || clip.z > clip.w + r) {
      return false;
    }
  }
  return true;
}

Diligent::Viewport buildViewport(int render_width, int render_height) {
  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(render_width);
  viewport.Height = static_cast<float>(render_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  return viewport;
}

#if defined(NDEBUG)
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_NONE;
#else
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_VERIFY_ALL;
#endif

bool updateSkinningConstants(Diligent::IDeviceContext* context,
                             Diligent::IBuffer* buffer,
                             const std::vector<glm::mat4>& palette) {
  if (context == nullptr || buffer == nullptr) {
    return false;
  }

  SkinningConstants constants{};
  const size_t count = std::min<size_t>(palette.size(), 128u);
  constants.params[0] = count > 0 ? 1.0f : 0.0f;
  constants.params[1] = static_cast<float>(count);
  for (size_t i = 0; i < count; ++i) {
    copyMat4(constants.matrices[i], palette[i]);
  }

  Diligent::MapHelper<SkinningConstants> mapped(
      context, buffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
  auto* mapped_constants = getMappedData(mapped);
  if (mapped_constants == nullptr) {
    return false;
  }
  *mapped_constants = constants;
  return true;
}

}  // namespace

void DiligentBackend::collectForwardLayerState(renderer::LayerId layer,
                                               const glm::mat4& view_proj,
                                               const glm::vec3& camera_position,
                                               const glm::vec3& camera_forward,
                                               bool is_gl,
                                               ForwardLayerState& out_state,
                                               ForwardLayerStats& out_stats) const {
  struct ForwardBatchKeyHash {
    size_t operator()(const ForwardBatchKey& key) const noexcept {
      size_t h = static_cast<size_t>(key.mesh);
      h ^= static_cast<size_t>(key.material) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.index_offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.index_count) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.indexed ? 1u : 0u) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.skinned ? 1u : 0u) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };

  out_state.opaque_batches.clear();
  out_state.skinned_opaque_draws.clear();
  out_state.transparent_draws.clear();
  out_state.pre_particle_scene_sample_draws.clear();
  out_state.post_particle_draws.clear();
  out_state.opaque_batches.reserve(instances_.size());
  out_state.skinned_opaque_draws.reserve(instances_.size() / 4 + 1);
  out_state.transparent_draws.reserve(instances_.size());
  out_state.pre_particle_scene_sample_draws.reserve(instances_.size() / 4 + 1);
  out_state.post_particle_draws.reserve(instances_.size() / 4 + 1);
  out_stats = ForwardLayerStats{};

  std::unordered_map<ForwardBatchKey, size_t, ForwardBatchKeyHash> opaque_batch_lookup;
  opaque_batch_lookup.reserve(instances_.size());

  auto append_opaque_forward_batch = [&](const ForwardBatchKey& key,
                                         const glm::mat4& transform,
                                         const std::vector<glm::mat4>& skinning_palette) {
    if (key.skinned) {
      out_state.skinned_opaque_draws.push_back(SkinnedForwardDraw{
          .key = key,
          .transform = transform,
          .skinning_palette = skinning_palette,
      });
      return;
    }
    auto it = opaque_batch_lookup.find(key);
    if (it == opaque_batch_lookup.end()) {
      const size_t idx = out_state.opaque_batches.size();
      out_state.opaque_batches.push_back(ForwardBatch{.key = key});
      opaque_batch_lookup.emplace(key, idx);
      it = opaque_batch_lookup.find(key);
    }
    out_state.opaque_batches[it->second].transforms.push_back(transform);
  };

  auto resolve_instance_material =
      [&](const InstanceRecord& instance,
          size_t submesh_index,
          renderer::MaterialId fallback_material) -> renderer::MaterialId {
    if (instance.material_set != renderer::kInvalidMaterialSet) {
      auto set_it = material_sets_.find(instance.material_set);
      if (set_it != material_sets_.end() &&
          set_it->second.source_mesh == instance.mesh &&
          submesh_index < set_it->second.materials.size()) {
        const renderer::MaterialId set_material = set_it->second.materials[submesh_index];
        if (set_material != renderer::kInvalidMaterial) {
          return set_material;
        }
      }
    }
    if (instance.material != renderer::kInvalidMaterial) {
      return instance.material;
    }
    return fallback_material;
  };

  auto lookup_material = [&](renderer::MaterialId material_id) -> const MaterialRecord* {
    if (material_id == renderer::kInvalidMaterial) {
      return nullptr;
    }
    auto mat_it = materials_.find(material_id);
    return mat_it != materials_.end() ? &mat_it->second : nullptr;
  };

  auto uses_post_particle_transparent_pass = [&](const MaterialRecord* mat) {
    if (!mat) {
      return false;
    }
    return mat->shading_model == renderer::MaterialDesc::ShadingModel::EnergyShell ||
           mat->shading_model == renderer::MaterialDesc::ShadingModel::SphereHalo ||
           mat->shading_model == renderer::MaterialDesc::ShadingModel::ScreenWave ||
           mat->shading_model == renderer::MaterialDesc::ShadingModel::SphereGlowVolume ||
           mat->shading_model == renderer::MaterialDesc::ShadingModel::VolumetricSolid;
  };

  auto uses_pre_particle_scene_sample_pass = [&](const MaterialRecord* mat) {
    if (!mat) {
      return false;
    }
    return mat->shading_model == renderer::MaterialDesc::ShadingModel::WaveVolume;
  };

  auto resolve_transparent_sort_depth = [&](const MaterialRecord* mat,
                                            const MeshRecord& mesh,
                                            const glm::mat4& transform) {
    glm::vec3 world_center =
        mesh.bounds_radius > 0.0f
            ? glm::vec3(transform * glm::vec4(mesh.bounds_center, 1.0f))
            : glm::vec3(transform[3]);
    if (mat && mat->shading_model == renderer::MaterialDesc::ShadingModel::VolumetricSolid) {
      world_center = mat->volume_center;
    }
    return glm::dot(world_center - camera_position, camera_forward);
  };

  for (const auto& entry : instances_) {
    const auto& instance = entry.second;
    if (instance.layer != layer) {
      out_stats.skipped_layer += 1;
      continue;
    }
    if (!instance.visible) {
      out_stats.skipped_hidden += 1;
      continue;
    }
    auto mesh_it = meshes_.find(instance.mesh);
    if (mesh_it == meshes_.end()) {
      out_stats.skipped_missing_mesh += 1;
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      out_stats.skipped_missing_vb += 1;
      continue;
    }
    if (mesh.bounds_radius > 0.0f) {
      const glm::vec4 world_bounds_sphere =
          transformBoundingSphere(instance.transform, mesh.bounds_center, mesh.bounds_radius);
      if (!sphereIntersectsClipVolume(view_proj, world_bounds_sphere, is_gl)) {
        out_stats.skipped_hidden += 1;
        continue;
      }
    }

    const bool indexed_mesh = mesh.index_buffer && mesh.index_count > 0;
    if (!mesh.submeshes.empty()) {
      for (size_t submesh_index = 0; submesh_index < mesh.submeshes.size(); ++submesh_index) {
        const auto& submesh = mesh.submeshes[submesh_index];
        const renderer::MaterialId mat_id =
            resolve_instance_material(instance, submesh_index, submesh.material);
        const ForwardBatchKey key{
            .mesh = instance.mesh,
            .material = mat_id,
            .index_offset = submesh.index_offset,
            .index_count = submesh.index_count,
            .indexed = indexed_mesh && submesh.index_count > 0,
            .skinned = instance.skinning_enabled,
        };
        const MaterialRecord* mat = lookup_material(mat_id);
        const bool transparent = (mat && mat->desc.transparent) || mesh.base_color.a < 0.999f;
        if (transparent) {
          auto& target_draws = uses_pre_particle_scene_sample_pass(mat)
                                   ? out_state.pre_particle_scene_sample_draws
                                   : (uses_post_particle_transparent_pass(mat)
                                          ? out_state.post_particle_draws
                                          : out_state.transparent_draws);
          target_draws.push_back(TransparentForwardDraw{
              .key = key,
              .transform = instance.transform,
              .skinning_palette = instance.skinning_palette,
              .depth = resolve_transparent_sort_depth(mat, mesh, instance.transform),
          });
        } else {
          append_opaque_forward_batch(key, instance.transform, instance.skinning_palette);
        }
      }
    } else {
      const renderer::MaterialId mat_id =
          resolve_instance_material(instance, 0, renderer::kInvalidMaterial);
      const ForwardBatchKey key{
          .mesh = instance.mesh,
          .material = mat_id,
          .index_offset = 0,
          .index_count = mesh.index_count,
          .indexed = indexed_mesh,
          .skinned = instance.skinning_enabled,
      };
      const MaterialRecord* mat = lookup_material(mat_id);
      const bool transparent = (mat && mat->desc.transparent) || mesh.base_color.a < 0.999f;
      if (transparent) {
        auto& target_draws = uses_pre_particle_scene_sample_pass(mat)
                                 ? out_state.pre_particle_scene_sample_draws
                                 : (uses_post_particle_transparent_pass(mat)
                                        ? out_state.post_particle_draws
                                        : out_state.transparent_draws);
        target_draws.push_back(TransparentForwardDraw{
            .key = key,
            .transform = instance.transform,
            .skinning_palette = instance.skinning_palette,
            .depth = resolve_transparent_sort_depth(mat, mesh, instance.transform),
        });
      } else {
        append_opaque_forward_batch(key, instance.transform, instance.skinning_palette);
      }
    }
  }

  std::sort(out_state.opaque_batches.begin(),
            out_state.opaque_batches.end(),
            [](const ForwardBatch& a, const ForwardBatch& b) {
              if (a.key.material != b.key.material) {
                return a.key.material < b.key.material;
              }
              if (a.key.mesh != b.key.mesh) {
                return a.key.mesh < b.key.mesh;
              }
              if (a.key.index_offset != b.key.index_offset) {
                return a.key.index_offset < b.key.index_offset;
              }
              if (a.key.index_count != b.key.index_count) {
                return a.key.index_count < b.key.index_count;
              }
              return static_cast<uint32_t>(a.key.indexed) < static_cast<uint32_t>(b.key.indexed);
            });

  auto compare_transparent_draws = [&](const TransparentForwardDraw& a,
                                       const TransparentForwardDraw& b) {
    if (a.depth != b.depth) {
      return a.depth > b.depth;
    }
    const MaterialRecord* mat_a = lookup_material(a.key.material);
    const MaterialRecord* mat_b = lookup_material(b.key.material);
    const bool additive_a =
        mat_a && mat_a->blend_mode == renderer::MaterialDesc::BlendMode::Additive;
    const bool additive_b =
        mat_b && mat_b->blend_mode == renderer::MaterialDesc::BlendMode::Additive;
    if (additive_a != additive_b) {
      return !additive_a && additive_b;
    }
    if (a.key.material != b.key.material) {
      return a.key.material < b.key.material;
    }
    if (a.key.mesh != b.key.mesh) {
      return a.key.mesh < b.key.mesh;
    }
    if (a.key.index_offset != b.key.index_offset) {
      return a.key.index_offset < b.key.index_offset;
    }
    return a.key.index_count < b.key.index_count;
  };

  std::sort(out_state.transparent_draws.begin(),
            out_state.transparent_draws.end(),
            compare_transparent_draws);
  std::sort(out_state.pre_particle_scene_sample_draws.begin(),
            out_state.pre_particle_scene_sample_draws.end(),
            compare_transparent_draws);
  std::sort(out_state.post_particle_draws.begin(),
            out_state.post_particle_draws.end(),
            compare_transparent_draws);
}

Diligent::Uint32 DiligentBackend::renderOpaqueForwardLayer(
    const ForwardLayerState& state,
    const DrawConstants& base_constants,
    Diligent::IPipelineState* active_forward_pipeline,
    bool use_custom_shader_override,
    Diligent::ITextureView* active_rtv,
    Diligent::ITextureView* active_dsv,
    int render_width,
    int render_height) {
  if (!active_forward_pipeline || !constants_) {
    return 0;
  }
  const bool draw_debug = [] {
    const char* value = std::getenv("KARMA_DRAW_DEBUG");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();

  auto ensure_instance_buffer = [&](size_t instance_count) {
    if (instance_count == 0) {
      return false;
    }
    if (instance_vb_ && instance_vb_capacity_ >= instance_count) {
      return true;
    }
    const size_t new_capacity =
        std::max(instance_count,
                 instance_vb_capacity_ > 0 ? instance_vb_capacity_ * 2 : static_cast<size_t>(128));
    Diligent::BufferDesc ib_desc{};
    ib_desc.Name = "Karma Instance Buffer";
    ib_desc.Usage = Diligent::USAGE_DYNAMIC;
    ib_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    ib_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    ib_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(InstanceTransformData));
    instance_vb_.Release();
    device_->CreateBuffer(ib_desc, nullptr, &instance_vb_);
    if (!instance_vb_) {
      return false;
    }
    instance_vb_capacity_ = new_capacity;
    return true;
  };

  auto is_valid_indexed_draw = [&](const MeshRecord& mesh,
                                   Diligent::Uint32 first_index,
                                   Diligent::Uint32 index_count) {
    if (!mesh.index_buffer || mesh.index_count == 0 || index_count == 0) {
      return false;
    }
    const uint64_t first = static_cast<uint64_t>(first_index);
    const uint64_t count = static_cast<uint64_t>(index_count);
    const uint64_t total = static_cast<uint64_t>(mesh.index_count);
    if (first >= total) {
      return false;
    }
    return first + count <= total;
  };

  auto lookup_material = [&](renderer::MaterialId material_id) -> const MaterialRecord* {
    if (material_id == renderer::kInvalidMaterial) {
      return nullptr;
    }
    auto mat_it = materials_.find(material_id);
    return mat_it != materials_.end() ? &mat_it->second : nullptr;
  };

  thread_local std::vector<InstanceTransformData> packed_transforms;
  auto pack_transforms = [&](const std::vector<glm::mat4>& transforms) {
    packed_transforms.clear();
    packed_transforms.reserve(transforms.size());
    for (const glm::mat4& transform : transforms) {
      InstanceTransformData packed{};
      const float* ptr = glm::value_ptr(transform);
      std::memcpy(packed.col0, ptr, sizeof(packed.col0));
      std::memcpy(packed.col1, ptr + 4, sizeof(packed.col1));
      std::memcpy(packed.col2, ptr + 8, sizeof(packed.col2));
      std::memcpy(packed.col3, ptr + 12, sizeof(packed.col3));
      packed_transforms.push_back(packed);
    }
  };

  auto update_forward_material_constants = [&](renderer::MaterialId material_id,
                                               renderer::MeshId mesh_id,
                                               const MeshRecord& mesh,
                                               const MaterialRecord* mat,
                                               renderer::MaterialId& last_constants_material,
                                               renderer::MeshId& last_constants_mesh) -> bool {
    if (material_id == last_constants_material &&
        (material_id != renderer::kInvalidMaterial || mesh_id == last_constants_mesh)) {
      return true;
    }
    DrawConstants constants = base_constants;
    glm::vec4 base_color = mat ? mat->base_color_factor : mesh.base_color;
    if (!mat && base_color == glm::vec4(1.0f)) {
      base_color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    }
    constants.base_color_factor[0] = base_color.r;
    constants.base_color_factor[1] = base_color.g;
    constants.base_color_factor[2] = base_color.b;
    constants.base_color_factor[3] = base_color.a;
    const glm::vec3 emissive = mat ? mat->emissive_factor : glm::vec3(0.0f);
    constants.emissive_factor[0] = emissive.x;
    constants.emissive_factor[1] = emissive.y;
    constants.emissive_factor[2] = emissive.z;
    constants.emissive_factor[3] = 1.0f;
    constants.pbr_params[0] = mat ? mat->metallic_factor : 1.0f;
    constants.pbr_params[1] = mat ? mat->roughness_factor : 1.0f;
    constants.pbr_params[2] = mat ? mat->occlusion_strength : 1.0f;
    constants.pbr_params[3] = mat ? mat->normal_scale : 1.0f;
    constants.material_params0[0] =
        mat ? static_cast<float>(static_cast<uint32_t>(mat->shading_model)) : 0.0f;
    constants.material_params0[1] = mat ? mat->shell_fresnel_power : 5.0f;
    constants.material_params0[2] = mat ? mat->shell_fresnel_strength : 1.0f;
    constants.material_params0[3] = mat ? mat->shell_refraction_strength : 0.08f;
    if (mat && (mat->shading_model == renderer::MaterialDesc::ShadingModel::WaveVolume ||
                mat->shading_model == renderer::MaterialDesc::ShadingModel::ScreenWave)) {
      constants.material_params1[0] = mat->wave_tint_strength;
      constants.material_params1[1] = mat->wave_distortion_strength;
      constants.material_params1[2] = mat->wave_edge_strength;
      constants.material_params1[3] = mat->wave_noise_strength;
    } else if (mat &&
               mat->shading_model == renderer::MaterialDesc::ShadingModel::VolumetricSolid) {
      constants.material_params0[1] = static_cast<float>(mat->volume_shape);
      constants.material_params0[2] = mat->volume_anisotropy;
      constants.material_params0[3] = mat->volume_absorption;
      constants.material_params1[0] = mat->volume_center.x;
      constants.material_params1[1] = mat->volume_center.y;
      constants.material_params1[2] = mat->volume_center.z;
      constants.material_params1[3] = mat->volume_radius;
      constants.material_params2[0] = mat->volume_axis_x.x;
      constants.material_params2[1] = mat->volume_axis_x.y;
      constants.material_params2[2] = mat->volume_axis_x.z;
      constants.material_params2[3] = mat->volume_capsule_half_length;
      constants.material_params3[0] = mat->volume_axis_y.x;
      constants.material_params3[1] = mat->volume_axis_y.y;
      constants.material_params3[2] = mat->volume_axis_y.z;
      constants.material_params3[3] = mat->volume_density;
      constants.material_params4[0] = mat->volume_axis_z.x;
      constants.material_params4[1] = mat->volume_axis_z.y;
      constants.material_params4[2] = mat->volume_axis_z.z;
	      constants.material_params4[3] = mat->volume_scattering;
	      constants.material_params5[0] = mat->volume_distortion_strength;
	      constants.material_params5[1] = mat->volume_noise_strength;
	      constants.material_params5[2] = 0.0f;
	      constants.material_params5[3] = 0.0f;
    } else {
      constants.material_params1[0] = mat ? mat->shell_interior_strength : 0.4f;
      constants.material_params1[1] = mat ? mat->shell_highlight_strength : 1.0f;
      constants.material_params1[2] = mat ? mat->shell_alpha_boost : 0.0f;
      constants.material_params1[3] = mat ? mat->shell_swirl_strength : 0.0f;
    }
    if (mat && mat->shading_model == renderer::MaterialDesc::ShadingModel::SphereHalo) {
      constants.material_params2[0] = mat->screen_center_x;
      constants.material_params2[1] = mat->screen_center_y;
      constants.material_params2[2] = mat->screen_radius_x;
      constants.material_params2[3] = mat->screen_radius_y;
    } else if (mat && mat->shading_model == renderer::MaterialDesc::ShadingModel::ScreenWave) {
      constants.material_params2[0] = mat->screen_center_x;
      constants.material_params2[1] = mat->screen_center_y;
      constants.material_params2[2] = mat->screen_radius_x;
      constants.material_params2[3] = mat->screen_radius_y;
    } else if (mat &&
               mat->shading_model == renderer::MaterialDesc::ShadingModel::VolumetricSolid) {
      // Volumetric solids pack params 2-5 above with shape axes and optical data.
    } else {
      constants.material_params2[0] = (mat && mat->analytic_sphere_normals) ? 1.0f : 0.0f;
      constants.material_params2[1] = mat ? mat->shell_body_strength : 1.0f;
      constants.material_params2[2] = (mat && mat->desc.unlit) ? 1.0f : 0.0f;
      constants.material_params2[3] = 0.0f;
    }
    {
      Diligent::MapHelper<DrawConstants> mapped(
          context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        return false;
      }
      *mapped_constants = constants;
    }
    last_constants_material = material_id;
    last_constants_mesh = mesh_id;
    return true;
  };

  auto resolve_forward_srb = [&](const MaterialRecord* mat) -> Diligent::IShaderResourceBinding* {
    if (use_custom_shader_override) {
      return camera_override_srb_;
    }
    if (mat && mat->srb) {
      return mat->srb;
    }
    return default_material_srb_ ? default_material_srb_ : shader_resources_;
  };

  auto bind_forward_geometry =
      [&](const MeshRecord& mesh,
          const std::vector<InstanceTransformData>& transforms,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb) -> bool {
    if (!ensure_instance_buffer(transforms.size())) {
      return false;
    }
    {
      Diligent::MapHelper<InstanceTransformData> instance_map(
          context_, instance_vb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_instances = getMappedData(instance_map);
      if (mapped_instances == nullptr) {
        return false;
      }
      std::memcpy(mapped_instances,
                  transforms.data(),
                  transforms.size() * sizeof(InstanceTransformData));
    }

    Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
    Diligent::IBuffer* instance_vb = instance_vb_.RawPtr();
    if (mesh_vb != bound_mesh_vb || instance_vb != bound_instance_vb) {
      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer, instance_vb_};
      Diligent::Uint64 offsets[] = {0, 0};
      context_->SetVertexBuffers(0,
                                 2,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      bound_mesh_vb = mesh_vb;
      bound_instance_vb = instance_vb;
    }
    return true;
  };

  auto draw_forward_batch = [&](const MeshRecord& mesh,
                                const ForwardBatchKey& key,
                                Diligent::Uint32 instance_count,
                                Diligent::IBuffer*& bound_index_buffer,
                                const char* pass_label) {
    auto log_draw = [&](const char* draw_kind) {
      if (!draw_debug) {
        return;
      }
      std::fprintf(stderr,
                   "[Karma][DrawDebug] pass=%s kind=%s mesh=%u material=%u "
                   "indexed=%s skinned=%s vertices=%u mesh_indices=%u "
                   "first_index=%u draw_indices=%u instances=%u vb=%s ib=%s\n",
                   pass_label,
                   draw_kind,
                   key.mesh,
                   key.material,
                   key.indexed ? "true" : "false",
                   key.skinned ? "true" : "false",
                   mesh.vertex_count,
                   mesh.index_count,
                   key.index_offset,
                   key.index_count,
                   instance_count,
                   mesh.vertex_buffer ? "yes" : "no",
                   mesh.index_buffer ? "yes" : "no");
      std::fflush(stderr);
    };
    if (key.indexed) {
      if (!is_valid_indexed_draw(mesh, key.index_offset, key.index_count)) {
        log_draw("invalid-indexed-skip");
        return false;
      }
      Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
      if (index_buffer != bound_index_buffer) {
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_index_buffer = index_buffer;
      }
      Diligent::DrawIndexedAttribs indexed{};
      indexed.IndexType = Diligent::VT_UINT32;
      indexed.NumIndices = key.index_count;
      indexed.FirstIndexLocation = key.index_offset;
      indexed.NumInstances = instance_count;
      indexed.Flags = kHotPathDrawFlags;
      log_draw("indexed");
      context_->DrawIndexed(indexed);
      return true;
    }

    Diligent::DrawAttribs draw_attrs{};
    draw_attrs.NumVertices = mesh.vertex_count;
    draw_attrs.NumInstances = instance_count;
    draw_attrs.Flags = kHotPathDrawFlags;
    log_draw("non-indexed");
    context_->Draw(draw_attrs);
    return true;
  };

  const Diligent::Viewport viewport = buildViewport(render_width, render_height);
  Diligent::Uint32 draw_count = 0;
  const auto& adapter_info = device_->GetAdapterInfo();
  const bool disable_depth_prepass_for_driver =
      device_->GetDeviceInfo().IsVulkanDevice() &&
      (adapter_info.Vendor == Diligent::ADAPTER_VENDOR_NVIDIA ||
       adapter_info.Vendor == Diligent::ADAPTER_VENDOR_INTEL);
  const bool disable_depth_prepass_for_env = [] {
    const char* value = std::getenv("KARMA_DISABLE_DEPTH_PREPASS");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();
  const bool force_depth_prepass_for_env = [] {
    const char* value = std::getenv("KARMA_FORCE_DEPTH_PREPASS");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();
  const bool run_depth_prepass =
      depth_prepass_pipeline_state_ && active_dsv && state.opaque_batches.size() > 1 &&
      (!disable_depth_prepass_for_driver || force_depth_prepass_for_env) &&
      !disable_depth_prepass_for_env &&
      !use_custom_shader_override;

  if (run_depth_prepass) {
    context_->SetRenderTargets(0,
                               nullptr,
                               active_dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->SetPipelineState(depth_prepass_pipeline_state_);
    bool depth_prepass_ready = true;
    {
      Diligent::MapHelper<DrawConstants> mapped(
          context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        depth_prepass_ready = false;
      } else {
        *mapped_constants = base_constants;
      }
    }

    Diligent::IBuffer* depth_bound_mesh_vb = nullptr;
    Diligent::IBuffer* depth_bound_instance_vb = nullptr;
    Diligent::IBuffer* depth_bound_index_buffer = nullptr;
    if (depth_prepass_ready) {
      updateSkinningConstants(context_, skinning_constants_, {});
      for (const auto& batch : state.opaque_batches) {
        if (batch.transforms.empty()) {
          continue;
        }
        auto mesh_it = meshes_.find(batch.key.mesh);
        if (mesh_it == meshes_.end()) {
          continue;
        }
        const auto& mesh = mesh_it->second;
        if (!mesh.vertex_buffer) {
          continue;
        }

        pack_transforms(batch.transforms);
        if (!bind_forward_geometry(
                mesh, packed_transforms, depth_bound_mesh_vb, depth_bound_instance_vb)) {
          continue;
        }
        draw_forward_batch(mesh,
                           batch.key,
                           static_cast<Diligent::Uint32>(packed_transforms.size()),
                           depth_bound_index_buffer,
                           "opaque-depth-prepass");
      }
      for (const auto& draw : state.skinned_opaque_draws) {
        auto mesh_it = meshes_.find(draw.key.mesh);
        if (mesh_it == meshes_.end()) {
          continue;
        }
        const auto& mesh = mesh_it->second;
        if (!mesh.vertex_buffer) {
          continue;
        }
        if (!updateSkinningConstants(context_, skinning_constants_, draw.skinning_palette)) {
          continue;
        }
        InstanceTransformData packed_transform{};
        const float* ptr = glm::value_ptr(draw.transform);
        std::memcpy(packed_transform.col0, ptr, sizeof(packed_transform.col0));
        std::memcpy(packed_transform.col1, ptr + 4, sizeof(packed_transform.col1));
        std::memcpy(packed_transform.col2, ptr + 8, sizeof(packed_transform.col2));
        std::memcpy(packed_transform.col3, ptr + 12, sizeof(packed_transform.col3));
        std::vector<InstanceTransformData> single_transform{packed_transform};
        if (!bind_forward_geometry(
                mesh, single_transform, depth_bound_mesh_vb, depth_bound_instance_vb)) {
          continue;
        }
        draw_forward_batch(mesh, draw.key, 1, depth_bound_index_buffer, "skinned-depth-prepass");
      }
    }

    context_->SetRenderTargets(1,
                               &active_rtv,
                               active_dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    context_->SetViewports(1,
                           &viewport,
                           static_cast<Diligent::Uint32>(render_width),
                           static_cast<Diligent::Uint32>(render_height));
  }

  context_->SetPipelineState(active_forward_pipeline);
  Diligent::IBuffer* bound_mesh_vb = nullptr;
  Diligent::IBuffer* bound_instance_vb = nullptr;
  Diligent::IBuffer* bound_index_buffer = nullptr;
  Diligent::IShaderResourceBinding* bound_forward_srb = nullptr;
  renderer::MaterialId last_constants_material = renderer::kInvalidMaterial;
  renderer::MeshId last_constants_mesh = renderer::kInvalidMesh;
  {
    Diligent::MapHelper<DrawConstants> mapped(
        context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    auto* mapped_constants = getMappedData(mapped);
    if (mapped_constants == nullptr) {
      return 0;
    }
    *mapped_constants = base_constants;
  }
  if (use_custom_shader_override && camera_override_srb_) {
    context_->CommitShaderResources(camera_override_srb_,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    bound_forward_srb = camera_override_srb_;
  }
  updateSkinningConstants(context_, skinning_constants_, {});

  for (const auto& batch : state.opaque_batches) {
    if (batch.transforms.empty()) {
      continue;
    }
    auto mesh_it = meshes_.find(batch.key.mesh);
    if (mesh_it == meshes_.end()) {
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      continue;
    }

    const MaterialRecord* mat = lookup_material(batch.key.material);
    if (!update_forward_material_constants(batch.key.material,
                                           batch.key.mesh,
                                           mesh,
                                           mat,
                                           last_constants_material,
                                           last_constants_mesh)) {
      continue;
    }

    if (!use_custom_shader_override) {
      Diligent::IShaderResourceBinding* srb = resolve_forward_srb(mat);
      if (srb && srb != bound_forward_srb) {
        context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_forward_srb = srb;
      }
    }

    pack_transforms(batch.transforms);
    if (!bind_forward_geometry(mesh, packed_transforms, bound_mesh_vb, bound_instance_vb)) {
      continue;
    }
    if (draw_forward_batch(mesh,
                           batch.key,
                           static_cast<Diligent::Uint32>(packed_transforms.size()),
                           bound_index_buffer,
                           "opaque-forward")) {
      draw_count += 1;
    }
  }

  for (const auto& draw : state.skinned_opaque_draws) {
    auto mesh_it = meshes_.find(draw.key.mesh);
    if (mesh_it == meshes_.end()) {
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      continue;
    }

    const MaterialRecord* mat = lookup_material(draw.key.material);
    if (!update_forward_material_constants(draw.key.material,
                                           draw.key.mesh,
                                           mesh,
                                           mat,
                                           last_constants_material,
                                           last_constants_mesh)) {
      continue;
    }

    if (!use_custom_shader_override) {
      Diligent::IShaderResourceBinding* srb = resolve_forward_srb(mat);
      if (srb && srb != bound_forward_srb) {
        context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_forward_srb = srb;
      }
    }

    if (!updateSkinningConstants(context_, skinning_constants_, draw.skinning_palette)) {
      continue;
    }

    InstanceTransformData packed_transform{};
    const float* ptr = glm::value_ptr(draw.transform);
    std::memcpy(packed_transform.col0, ptr, sizeof(packed_transform.col0));
    std::memcpy(packed_transform.col1, ptr + 4, sizeof(packed_transform.col1));
    std::memcpy(packed_transform.col2, ptr + 8, sizeof(packed_transform.col2));
    std::memcpy(packed_transform.col3, ptr + 12, sizeof(packed_transform.col3));
    if (!bind_forward_geometry(mesh,
                               std::vector<InstanceTransformData>{packed_transform},
                               bound_mesh_vb,
                               bound_instance_vb)) {
      continue;
    }
    if (draw_forward_batch(mesh, draw.key, 1, bound_index_buffer, "skinned-forward")) {
      draw_count += 1;
    }
  }

  return draw_count;
}

Diligent::Uint32 DiligentBackend::renderTransparentForwardDraws(
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
    Diligent::ITextureView* scene_depth_sample_srv) {
  if (draws.empty() || !active_forward_pipeline || !constants_) {
    return 0;
  }
  const bool draw_debug = [] {
    const char* value = std::getenv("KARMA_DRAW_DEBUG");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();

  auto ensure_instance_buffer = [&](size_t instance_count) {
    if (instance_count == 0) {
      return false;
    }
    if (instance_vb_ && instance_vb_capacity_ >= instance_count) {
      return true;
    }
    const size_t new_capacity =
        std::max(instance_count,
                 instance_vb_capacity_ > 0 ? instance_vb_capacity_ * 2 : static_cast<size_t>(128));
    Diligent::BufferDesc ib_desc{};
    ib_desc.Name = "Karma Instance Buffer";
    ib_desc.Usage = Diligent::USAGE_DYNAMIC;
    ib_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    ib_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    ib_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(InstanceTransformData));
    instance_vb_.Release();
    device_->CreateBuffer(ib_desc, nullptr, &instance_vb_);
    if (!instance_vb_) {
      return false;
    }
    instance_vb_capacity_ = new_capacity;
    return true;
  };

  auto is_valid_indexed_draw = [&](const MeshRecord& mesh,
                                   Diligent::Uint32 first_index,
                                   Diligent::Uint32 index_count) {
    if (!mesh.index_buffer || mesh.index_count == 0 || index_count == 0) {
      return false;
    }
    const uint64_t first = static_cast<uint64_t>(first_index);
    const uint64_t count = static_cast<uint64_t>(index_count);
    const uint64_t total = static_cast<uint64_t>(mesh.index_count);
    if (first >= total) {
      return false;
    }
    return first + count <= total;
  };

  auto lookup_material = [&](renderer::MaterialId material_id) -> const MaterialRecord* {
    if (material_id == renderer::kInvalidMaterial) {
      return nullptr;
    }
    auto mat_it = materials_.find(material_id);
    return mat_it != materials_.end() ? &mat_it->second : nullptr;
  };

  auto update_forward_material_constants = [&](renderer::MaterialId material_id,
                                               renderer::MeshId mesh_id,
                                               const MeshRecord& mesh,
                                               const MaterialRecord* mat,
                                               renderer::MaterialId& last_constants_material,
                                               renderer::MeshId& last_constants_mesh) -> bool {
    if (material_id == last_constants_material &&
        (material_id != renderer::kInvalidMaterial || mesh_id == last_constants_mesh)) {
      return true;
    }
    DrawConstants constants = base_constants;
    glm::vec4 base_color = mat ? mat->base_color_factor : mesh.base_color;
    if (!mat && base_color == glm::vec4(1.0f)) {
      base_color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    }
    constants.base_color_factor[0] = base_color.r;
    constants.base_color_factor[1] = base_color.g;
    constants.base_color_factor[2] = base_color.b;
    constants.base_color_factor[3] = base_color.a;
    const glm::vec3 emissive = mat ? mat->emissive_factor : glm::vec3(0.0f);
    constants.emissive_factor[0] = emissive.x;
    constants.emissive_factor[1] = emissive.y;
    constants.emissive_factor[2] = emissive.z;
    constants.emissive_factor[3] = 1.0f;
    constants.pbr_params[0] = mat ? mat->metallic_factor : 1.0f;
    constants.pbr_params[1] = mat ? mat->roughness_factor : 1.0f;
    constants.pbr_params[2] = mat ? mat->occlusion_strength : 1.0f;
    constants.pbr_params[3] = mat ? mat->normal_scale : 1.0f;
    constants.material_params0[0] =
        mat ? static_cast<float>(static_cast<uint32_t>(mat->shading_model)) : 0.0f;
    constants.material_params0[1] = mat ? mat->shell_fresnel_power : 5.0f;
    constants.material_params0[2] = mat ? mat->shell_fresnel_strength : 1.0f;
    constants.material_params0[3] = mat ? mat->shell_refraction_strength : 0.08f;
    if (mat && (mat->shading_model == renderer::MaterialDesc::ShadingModel::WaveVolume ||
                mat->shading_model == renderer::MaterialDesc::ShadingModel::ScreenWave)) {
      constants.material_params1[0] = mat->wave_tint_strength;
      constants.material_params1[1] = mat->wave_distortion_strength;
      constants.material_params1[2] = mat->wave_edge_strength;
      constants.material_params1[3] = mat->wave_noise_strength;
    } else if (mat &&
               mat->shading_model == renderer::MaterialDesc::ShadingModel::VolumetricSolid) {
      constants.material_params0[1] = static_cast<float>(mat->volume_shape);
      constants.material_params0[2] = mat->volume_anisotropy;
      constants.material_params0[3] = mat->volume_absorption;
      constants.material_params1[0] = mat->volume_center.x;
      constants.material_params1[1] = mat->volume_center.y;
      constants.material_params1[2] = mat->volume_center.z;
      constants.material_params1[3] = mat->volume_radius;
      constants.material_params2[0] = mat->volume_axis_x.x;
      constants.material_params2[1] = mat->volume_axis_x.y;
      constants.material_params2[2] = mat->volume_axis_x.z;
      constants.material_params2[3] = mat->volume_capsule_half_length;
      constants.material_params3[0] = mat->volume_axis_y.x;
      constants.material_params3[1] = mat->volume_axis_y.y;
      constants.material_params3[2] = mat->volume_axis_y.z;
      constants.material_params3[3] = mat->volume_density;
      constants.material_params4[0] = mat->volume_axis_z.x;
      constants.material_params4[1] = mat->volume_axis_z.y;
      constants.material_params4[2] = mat->volume_axis_z.z;
	      constants.material_params4[3] = mat->volume_scattering;
	      constants.material_params5[0] = mat->volume_distortion_strength;
	      constants.material_params5[1] = mat->volume_noise_strength;
	      constants.material_params5[2] = 0.0f;
	      constants.material_params5[3] = 0.0f;
    } else {
      constants.material_params1[0] = mat ? mat->shell_interior_strength : 0.4f;
      constants.material_params1[1] = mat ? mat->shell_highlight_strength : 1.0f;
      constants.material_params1[2] = mat ? mat->shell_alpha_boost : 0.0f;
      constants.material_params1[3] = mat ? mat->shell_swirl_strength : 0.0f;
    }
    if (mat && mat->shading_model == renderer::MaterialDesc::ShadingModel::SphereHalo) {
      constants.material_params2[0] = mat->screen_center_x;
      constants.material_params2[1] = mat->screen_center_y;
      constants.material_params2[2] = mat->screen_radius_x;
      constants.material_params2[3] = mat->screen_radius_y;
    } else if (mat && mat->shading_model == renderer::MaterialDesc::ShadingModel::ScreenWave) {
      constants.material_params2[0] = mat->screen_center_x;
      constants.material_params2[1] = mat->screen_center_y;
      constants.material_params2[2] = mat->screen_radius_x;
      constants.material_params2[3] = mat->screen_radius_y;
    } else if (mat &&
               mat->shading_model == renderer::MaterialDesc::ShadingModel::VolumetricSolid) {
      // Volumetric solids pack params 2-5 above with shape axes and optical data.
    } else {
      constants.material_params2[0] = (mat && mat->analytic_sphere_normals) ? 1.0f : 0.0f;
      constants.material_params2[1] = mat ? mat->shell_body_strength : 1.0f;
      constants.material_params2[2] = (mat && mat->desc.unlit) ? 1.0f : 0.0f;
      constants.material_params2[3] = 0.0f;
    }
    {
      Diligent::MapHelper<DrawConstants> mapped(
          context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        return false;
      }
      *mapped_constants = constants;
    }
    last_constants_material = material_id;
    last_constants_mesh = mesh_id;
    return true;
  };

  auto resolve_forward_pipeline =
      [&](const MaterialRecord* mat) -> Diligent::IPipelineState* {
    if (use_custom_shader_override) {
      return active_forward_pipeline;
    }
    const bool additive = mat && mat->blend_mode == renderer::MaterialDesc::BlendMode::Additive;
    const bool double_sided = mat && mat->desc.double_sided;
    if (additive) {
      if (double_sided && additive_double_sided_pipeline_state_) {
        return additive_double_sided_pipeline_state_;
      }
      if (additive_pipeline_state_) {
        return additive_pipeline_state_;
      }
    }
    if (double_sided && transparent_double_sided_pipeline_state_) {
      return transparent_double_sided_pipeline_state_;
    }
    if (transparent_pipeline_state_) {
      return transparent_pipeline_state_;
    }
    return active_forward_pipeline;
  };

  auto resolve_forward_srb = [&](const MaterialRecord* mat,
                                 Diligent::IPipelineState* pipeline) -> Diligent::IShaderResourceBinding* {
    if (use_custom_shader_override) {
      return camera_override_srb_;
    }
    const bool using_transparent_pipeline = pipeline != active_forward_pipeline;
    if (!using_transparent_pipeline) {
      if (mat && mat->srb) {
        return mat->srb;
      }
      return default_material_srb_ ? default_material_srb_ : shader_resources_;
    }

    const bool double_sided = mat && mat->desc.double_sided;
    const bool additive = mat && mat->blend_mode == renderer::MaterialDesc::BlendMode::Additive;
    if (double_sided) {
      if (additive) {
        if (mat && mat->additive_double_sided_srb) {
          return mat->additive_double_sided_srb;
        }
        if (additive_double_sided_default_material_srb_) {
          return additive_double_sided_default_material_srb_;
        }
      }
      if (mat && mat->transparent_double_sided_srb) {
        return mat->transparent_double_sided_srb;
      }
      if (transparent_double_sided_default_material_srb_) {
        return transparent_double_sided_default_material_srb_;
      }
    }
    if (additive) {
      if (mat && mat->additive_srb) {
        return mat->additive_srb;
      }
      if (additive_default_material_srb_) {
        return additive_default_material_srb_;
      }
    }
    if (mat && mat->transparent_srb) {
      return mat->transparent_srb;
    }
    if (transparent_default_material_srb_) {
      return transparent_default_material_srb_;
    }
    if (mat && mat->srb) {
      return mat->srb;
    }
    return default_material_srb_ ? default_material_srb_ : shader_resources_;
  };

  auto bind_forward_geometry =
      [&](const MeshRecord& mesh,
          const InstanceTransformData* transforms,
          size_t transform_count,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb) -> bool {
    if (!ensure_instance_buffer(transform_count)) {
      return false;
    }
    {
      Diligent::MapHelper<InstanceTransformData> instance_map(
          context_, instance_vb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_instances = getMappedData(instance_map);
      if (mapped_instances == nullptr) {
        return false;
      }
      std::memcpy(mapped_instances, transforms, transform_count * sizeof(InstanceTransformData));
    }

    Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
    Diligent::IBuffer* instance_vb = instance_vb_.RawPtr();
    if (mesh_vb != bound_mesh_vb || instance_vb != bound_instance_vb) {
      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer, instance_vb_};
      Diligent::Uint64 offsets[] = {0, 0};
      context_->SetVertexBuffers(0,
                                 2,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      bound_mesh_vb = mesh_vb;
      bound_instance_vb = instance_vb;
    }
    return true;
  };

  auto draw_forward_batch = [&](const MeshRecord& mesh,
                                const ForwardBatchKey& key,
                                Diligent::Uint32 instance_count,
                                Diligent::IBuffer*& bound_index_buffer) {
    auto log_draw = [&](const char* draw_kind) {
      if (!draw_debug) {
        return;
      }
      std::fprintf(stderr,
                   "[Karma][DrawDebug] pass=transparent-forward kind=%s mesh=%u material=%u "
                   "indexed=%s skinned=%s vertices=%u mesh_indices=%u "
                   "first_index=%u draw_indices=%u instances=%u vb=%s ib=%s\n",
                   draw_kind,
                   key.mesh,
                   key.material,
                   key.indexed ? "true" : "false",
                   key.skinned ? "true" : "false",
                   mesh.vertex_count,
                   mesh.index_count,
                   key.index_offset,
                   key.index_count,
                   instance_count,
                   mesh.vertex_buffer ? "yes" : "no",
                   mesh.index_buffer ? "yes" : "no");
      std::fflush(stderr);
    };
    if (key.indexed) {
      if (!is_valid_indexed_draw(mesh, key.index_offset, key.index_count)) {
        log_draw("invalid-indexed-skip");
        return false;
      }
      Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
      if (index_buffer != bound_index_buffer) {
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_index_buffer = index_buffer;
      }
      Diligent::DrawIndexedAttribs indexed{};
      indexed.IndexType = Diligent::VT_UINT32;
      indexed.NumIndices = key.index_count;
      indexed.FirstIndexLocation = key.index_offset;
      indexed.NumInstances = instance_count;
      indexed.Flags = kHotPathDrawFlags;
      log_draw("indexed");
      context_->DrawIndexed(indexed);
      return true;
    }

    Diligent::DrawAttribs draw_attrs{};
    draw_attrs.NumVertices = mesh.vertex_count;
    draw_attrs.NumInstances = instance_count;
    draw_attrs.Flags = kHotPathDrawFlags;
    log_draw("non-indexed");
    context_->Draw(draw_attrs);
    return true;
  };

  ensureParticleFallbackDepthResource();
  const Diligent::Viewport viewport = buildViewport(render_width, render_height);
  Diligent::ITextureView* transparent_dsv = particle_dsv ? particle_dsv : active_dsv;
  context_->SetRenderTargets(1,
                             &active_rtv,
                             transparent_dsv,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context_->SetViewports(1,
                         &viewport,
                         static_cast<Diligent::Uint32>(render_width),
                         static_cast<Diligent::Uint32>(render_height));

  Diligent::IPipelineState* bound_forward_pipeline = nullptr;
  Diligent::IShaderResourceBinding* bound_forward_srb = nullptr;
  Diligent::IBuffer* bound_mesh_vb = nullptr;
  Diligent::IBuffer* bound_instance_vb = nullptr;
  Diligent::IBuffer* bound_index_buffer = nullptr;
  Diligent::ITextureView* bound_scene_color = nullptr;
  Diligent::ITextureView* bound_scene_depth = nullptr;
  renderer::MaterialId last_constants_material = renderer::kInvalidMaterial;
  renderer::MeshId last_constants_mesh = renderer::kInvalidMesh;
  Diligent::Uint32 draw_count = 0;
  const std::vector<glm::mat4> empty_skinning_palette;

  for (const auto& draw : draws) {
    auto mesh_it = meshes_.find(draw.key.mesh);
    if (mesh_it == meshes_.end()) {
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      continue;
    }

    const MaterialRecord* mat = lookup_material(draw.key.material);
    Diligent::IPipelineState* pipeline = resolve_forward_pipeline(mat);
    if (!pipeline) {
      continue;
    }
    if (pipeline != bound_forward_pipeline) {
      context_->SetPipelineState(pipeline);
      bound_forward_pipeline = pipeline;
      bound_forward_srb = nullptr;
      bound_mesh_vb = nullptr;
      bound_instance_vb = nullptr;
      bound_index_buffer = nullptr;
    }

    if (!update_forward_material_constants(draw.key.material,
                                           draw.key.mesh,
                                           mesh,
                                           mat,
                                           last_constants_material,
                                           last_constants_mesh)) {
      continue;
    }

    Diligent::IShaderResourceBinding* srb = resolve_forward_srb(mat, pipeline);
    if (mat &&
        (mat->shading_model == renderer::MaterialDesc::ShadingModel::WaveVolume ||
         mat->shading_model == renderer::MaterialDesc::ShadingModel::VolumetricSolid) &&
        srb) {
      Diligent::ITextureView* desired_scene_color =
          scene_color_sample_srv ? scene_color_sample_srv : default_base_color_;
      Diligent::ITextureView* desired_scene_depth =
          scene_depth_sample_srv ? scene_depth_sample_srv : particle_fallback_depth_srv_;
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneColor")) {
        if (desired_scene_color != bound_scene_color || srb != bound_forward_srb) {
          var->Set(desired_scene_color);
          bound_scene_color = desired_scene_color;
          bound_forward_srb = nullptr;
        }
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth")) {
        if (desired_scene_depth != bound_scene_depth || srb != bound_forward_srb) {
          var->Set(desired_scene_depth);
          bound_scene_depth = desired_scene_depth;
          bound_forward_srb = nullptr;
        }
      }
    }
    if (srb && srb != bound_forward_srb) {
      context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      bound_forward_srb = srb;
    }

    if (!updateSkinningConstants(context_,
                                 skinning_constants_,
                                 draw.key.skinned ? draw.skinning_palette
                                                  : empty_skinning_palette)) {
      continue;
    }

    InstanceTransformData packed_transform{};
    const float* ptr = glm::value_ptr(draw.transform);
    std::memcpy(packed_transform.col0, ptr, sizeof(packed_transform.col0));
    std::memcpy(packed_transform.col1, ptr + 4, sizeof(packed_transform.col1));
    std::memcpy(packed_transform.col2, ptr + 8, sizeof(packed_transform.col2));
    std::memcpy(packed_transform.col3, ptr + 12, sizeof(packed_transform.col3));
    if (!bind_forward_geometry(mesh, &packed_transform, 1, bound_mesh_vb, bound_instance_vb)) {
      continue;
    }
    if (draw_forward_batch(mesh, draw.key, 1, bound_index_buffer)) {
      draw_count += 1;
    }
  }

  return draw_count;
}

bool DiligentBackend::forwardDrawsRequireSceneColorCopy(
    const std::vector<TransparentForwardDraw>& draws) const {
  for (const auto& draw : draws) {
    if (draw.key.material == renderer::kInvalidMaterial) {
      continue;
    }
    auto mat_it = materials_.find(draw.key.material);
    if (mat_it == materials_.end()) {
      continue;
    }
    const auto& mat = mat_it->second;
    if (mat.shading_model == renderer::MaterialDesc::ShadingModel::WaveVolume ||
        mat.shading_model == renderer::MaterialDesc::ShadingModel::VolumetricSolid) {
      return true;
    }
  }
  return false;
}

}  // namespace karma::renderer_backend
