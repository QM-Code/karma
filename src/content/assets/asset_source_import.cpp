#include "asset_source_import.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "karma/assets.h"
#include "karma/core.h"
#include "karma/visual.h"

#include "asset_texture_internal.h"
#include "../importers/gltf_scene_import_internal.h"
#include "../importers/mesh_import_internal.h"

namespace karma::assets {

namespace {

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

bool startupDiagnosticsEnabled() {
  static const bool enabled = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  return enabled;
}

void logSourceImportDiag(const char* type,
                         const std::string& key,
                         const std::filesystem::path& path,
                         const char* stage,
                         core::SteadyClock::time_point start,
                         core::SteadyClock::time_point end) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info(
      "Engine startup diag: area=asset_source_import type={} key='{}' path='{}' stage={} ms={:.2f}",
      type ? type : "unknown",
      key,
      path.string(),
      stage ? stage : "unknown",
      core::elapsedMilliseconds(start, end));
}

void logSourceImportDiag(const char* type,
                         const std::string& key,
                         const std::filesystem::path& path,
                         const char* stage,
                         core::SteadyClock::time_point start,
                         core::SteadyClock::time_point end,
                         std::size_t count) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info(
      "Engine startup diag: area=asset_source_import type={} key='{}' path='{}' stage={} ms={:.2f} count={}",
      type ? type : "unknown",
      key,
      path.string(),
      stage ? stage : "unknown",
      core::elapsedMilliseconds(start, end),
      count);
}

std::string childKey(const std::string& parent,
                     std::string_view kind,
                     std::size_t index) {
  return parent + "/" + std::string(kind) + "/" + std::to_string(index);
}

void assignSingleMaterialSlot(world::MeshData& mesh,
                              std::string slot_name,
                              std::string material_key) {
  mesh.material_slots = {world::MeshMaterialSlot{
      .name = std::move(slot_name),
      .default_material_key = std::move(material_key),
  }};
  if (mesh.submeshes.empty() && !mesh.indices.empty()) {
    mesh.submeshes.push_back(world::MeshSubmesh{
        .index_offset = 0u,
        .index_count = static_cast<uint32_t>(mesh.indices.size()),
        .material_slot = 0u,
    });
    return;
  }
  for (world::MeshSubmesh& submesh : mesh.submeshes) {
    submesh.material_slot = 0u;
  }
}

world::MeshData combineMeshes(std::vector<world::MeshData> meshes) {
  world::MeshData combined{};
  for (world::MeshData& mesh : meshes) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
      continue;
    }

    const uint32_t vertex_base = static_cast<uint32_t>(combined.vertices.size());
    const uint32_t index_base = static_cast<uint32_t>(combined.indices.size());
    combined.vertices.insert(combined.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    combined.normals.insert(combined.normals.end(), mesh.normals.begin(), mesh.normals.end());
    combined.uvs.insert(combined.uvs.end(), mesh.uvs.begin(), mesh.uvs.end());
    combined.uvs1.insert(combined.uvs1.end(), mesh.uvs1.begin(), mesh.uvs1.end());
    combined.tangents.insert(combined.tangents.end(), mesh.tangents.begin(), mesh.tangents.end());
    combined.joint_indices.insert(combined.joint_indices.end(),
                                  mesh.joint_indices.begin(),
                                  mesh.joint_indices.end());
    combined.joint_weights.insert(combined.joint_weights.end(),
                                  mesh.joint_weights.begin(),
                                  mesh.joint_weights.end());

    for (uint32_t index : mesh.indices) {
      combined.indices.push_back(vertex_base + index);
    }

    const uint32_t material_slot_base =
        static_cast<uint32_t>(combined.material_slots.size());
    combined.material_slots.insert(combined.material_slots.end(),
                                   mesh.material_slots.begin(),
                                   mesh.material_slots.end());
    if (!mesh.submeshes.empty()) {
      for (const world::MeshSubmesh& submesh : mesh.submeshes) {
        combined.submeshes.push_back(world::MeshSubmesh{
            .index_offset = index_base + submesh.index_offset,
            .index_count = submesh.index_count,
            .material_slot = material_slot_base + submesh.material_slot,
        });
      }
    } else {
      combined.submeshes.push_back(world::MeshSubmesh{
          .index_offset = index_base,
          .index_count = static_cast<uint32_t>(mesh.indices.size()),
          .material_slot = material_slot_base,
      });
    }
  }
  return combined;
}

}  // namespace

namespace detail {

bool importMeshAsset(AssetRegistry& assets,
                     const std::string& key,
                     const std::filesystem::path& path) {
  if (!AssetRegistry::isValidAssetKey(key)) {
    return false;
  }
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  std::vector<world::MeshData> imported = importMeshes(path);
  logSourceImportDiag("mesh",
                      key,
                      path,
                      "import meshes",
                      stage_start,
                      core::SteadyClock::now(),
                      imported.size());
  stage_start = core::SteadyClock::now();
  world::MeshData combined = combineMeshes(std::move(imported));
  logSourceImportDiag("mesh", key, path, "combine meshes", stage_start, core::SteadyClock::now());
  if (combined.vertices.empty() || combined.indices.empty()) {
    return false;
  }
  stage_start = core::SteadyClock::now();
  const bool registered = assets.registerMeshAsset(key, std::move(combined));
  logSourceImportDiag("mesh", key, path, "register", stage_start, core::SteadyClock::now());
  logSourceImportDiag("mesh", key, path, "total", total_start, core::SteadyClock::now());
  return registered;
}

bool importTextureAsset(AssetRegistry& assets,
                        const std::string& key,
                        const std::filesystem::path& path,
                        const TextureImportOptions& options) {
  if (!AssetRegistry::isValidAssetKey(key)) {
    return false;
  }
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  AssetCache cache;
  const nlohmann::json cache_options{
      {"srgb", options.srgb},
      {"generate_mips", options.generate_mips},
      {"alpha_bleed", options.alpha_bleed},
      {"alpha_coverage_cutoff", options.alpha_coverage_cutoff},
      {"semantic", static_cast<uint32_t>(options.semantic)},
      {"prefer_compressed", options.prefer_compressed},
      {"texture_profile", "ktx2_basis_uastc_zstd_rgba8_fallback"},
  };
  const std::string cache_key =
      cache.makeSourceKey(path,
                          textureImporterVersion(),
                          cache_options,
                          {},
                          textureDependencyVersion());
  logSourceImportDiag("texture", key, path, "cache key", stage_start, core::SteadyClock::now());
  stage_start = core::SteadyClock::now();
  if (auto cached = cache.readTexture(cache_key)) {
    logSourceImportDiag("texture", key, path, "cache read hit", stage_start, core::SteadyClock::now());
    stage_start = core::SteadyClock::now();
    const bool registered = assets.registerTextureAsset(key, std::move(*cached));
    logSourceImportDiag("texture", key, path, "register cached", stage_start, core::SteadyClock::now());
    logSourceImportDiag("texture", key, path, "total cache hit", total_start, core::SteadyClock::now());
    return registered;
  }
  logSourceImportDiag("texture", key, path, "cache read miss", stage_start, core::SteadyClock::now());

  stage_start = core::SteadyClock::now();
  std::optional<Rgba8Image> image = loadRgba8Image(path);
  logSourceImportDiag("texture", key, path, "image decode", stage_start, core::SteadyClock::now());
  if (!image.has_value() || !image->valid()) {
    return false;
  }
  if (options.alpha_bleed) {
    stage_start = core::SteadyClock::now();
    bleedTransparentRgb(*image);
    logSourceImportDiag("texture", key, path, "alpha bleed", stage_start, core::SteadyClock::now());
  }
  stage_start = core::SteadyClock::now();
  TextureAsset texture = makeTextureAssetFromImage(std::move(*image),
                                                   options.srgb,
                                                   options.generate_mips,
                                                   options.semantic,
                                                   options.prefer_compressed,
                                                   options.alpha_bleed && options.generate_mips,
                                                   options.alpha_coverage_cutoff);
  logSourceImportDiag("texture", key, path, "texture asset build", stage_start, core::SteadyClock::now());
  std::string diagnostic;
  stage_start = core::SteadyClock::now();
  (void)cache.writeTexture(cache_key, texture, &diagnostic);
  logSourceImportDiag("texture", key, path, "cache write", stage_start, core::SteadyClock::now());
  stage_start = core::SteadyClock::now();
  const bool registered = assets.registerTextureAsset(key, std::move(texture));
  logSourceImportDiag("texture", key, path, "register", stage_start, core::SteadyClock::now());
  logSourceImportDiag("texture", key, path, "total source import", total_start, core::SteadyClock::now());
  return registered;
}

bool importParticleEffect(AssetRegistry& assets,
                          const std::string& key,
                          const std::filesystem::path& path) {
  if (!AssetRegistry::isValidAssetKey(key)) {
    return false;
  }
  visual::particles::ParticleEffectAsset effect{};
  if (!visual::particles::loadParticleEffectAsset(path, effect)) {
    return false;
  }
  return assets.registerParticleEffect(key, std::move(effect));
}

GltfSceneAsset importGltfSceneAsset(AssetRegistry& assets,
                                    const std::string& key,
                                    const std::filesystem::path& path,
                                    const world::GltfSceneLoadOptions& options) {
  if (!AssetRegistry::isValidAssetKey(key)) {
    return {};
  }
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  world::GltfScenePrefab prefab = world::loadGltfScenePrefab(path, options);
  logSourceImportDiag("gltf_scene",
                      key,
                      path,
                      "load prefab",
                      stage_start,
                      core::SteadyClock::now(),
                      prefab.nodes.size());
  if (!prefab.valid()) {
    return {};
  }

  stage_start = core::SteadyClock::now();
  GltfSceneAsset asset{};
  asset.source_path = prefab.source_path;
  asset.scene_key = key + "/scene";
  asset.root_node = prefab.root_node;
  asset.nodes.reserve(prefab.nodes.size());

  std::size_t mesh_index = 0;
  std::size_t material_index = 0;
  std::size_t primitive_count = 0;
  for (const world::GltfScenePrefabNode& node : prefab.nodes) {
    GltfSceneAssetNode asset_node{};
    asset_node.name = node.name;
    asset_node.local_position = node.local_position;
    asset_node.local_rotation = node.local_rotation;
    asset_node.local_scale = node.local_scale;
    asset_node.world_position = node.world_position;
    asset_node.world_rotation = node.world_rotation;
    asset_node.world_scale = node.world_scale;
    asset_node.has_light = node.has_light;
    asset_node.light = node.light;
    asset_node.children = node.children;
    asset_node.primitives.reserve(node.primitives.size());

    for (const world::GltfScenePrefabPrimitive& primitive : node.primitives) {
      ++primitive_count;
      const std::string mesh_key = childKey(key, "meshes", mesh_index++);
      const std::string material_key = childKey(key, "materials", material_index++);
      rendering::MaterialAssetDesc material{};
      material.surface = primitive.material;
      material.material_asset_path = prefab.source_path;
      material.material_asset_index = primitive.source_material_index;
      if (primitive.source_material_index < prefab.imported_materials.size()) {
        material.imported_material = prefab.imported_materials[primitive.source_material_index];
      }
      std::vector<std::string> texture_keys =
          assets.registerImportedMaterialTextures(material_key, material);
      if (!assets.registerMaterialAsset(material_key, std::move(material))) {
        return {};
      }
      asset.texture_asset_keys.insert(asset.texture_asset_keys.end(),
                                      texture_keys.begin(),
                                      texture_keys.end());
      asset.material_keys.push_back(material_key);

      world::MeshData mesh = primitive.mesh;
      assignSingleMaterialSlot(mesh,
                               primitive.name.empty() ? std::string("Slot 0") : primitive.name,
                               material_key);
      if (!assets.registerMeshAsset(mesh_key, std::move(mesh))) {
        return {};
      }
      asset.mesh_asset_keys.push_back(mesh_key);

      asset_node.primitives.push_back(GltfSceneAssetPrimitive{
          .name = primitive.name,
          .mesh_key = mesh_key,
          .material_key = material_key,
          .skin_index = primitive.skin_index,
          .morph_weights = primitive.morph_weights,
          .joint_node_indices = primitive.joint_node_indices,
          .inverse_bind_matrices = primitive.inverse_bind_matrices,
      });
    }
    asset.nodes.push_back(std::move(asset_node));
  }
  logSourceImportDiag("gltf_scene",
                      key,
                      path,
                      "register node primitives",
                      stage_start,
                      core::SteadyClock::now(),
                      primitive_count);

  stage_start = core::SteadyClock::now();
  for (std::size_t i = 0; i < prefab.animations.size(); ++i) {
    const std::string clip_key = childKey(key, "animation_clips", i);
    if (!assets.registerAnimationClip(clip_key, prefab.animations[i])) {
      return {};
    }
    asset.animation_clip_keys.push_back(clip_key);
  }
  logSourceImportDiag("gltf_scene",
                      key,
                      path,
                      "register animations",
                      stage_start,
                      core::SteadyClock::now(),
                      asset.animation_clip_keys.size());
  stage_start = core::SteadyClock::now();
  for (std::size_t i = 0; i < prefab.skeletons.size(); ++i) {
    const std::string skeleton_key = childKey(key, "skeletons", i);
    if (!assets.registerSkeleton(skeleton_key, prefab.skeletons[i])) {
      return {};
    }
    asset.skeleton_keys.push_back(skeleton_key);
  }
  logSourceImportDiag("gltf_scene",
                      key,
                      path,
                      "register skeletons",
                      stage_start,
                      core::SteadyClock::now(),
                      asset.skeleton_keys.size());
  stage_start = core::SteadyClock::now();
  for (std::size_t i = 0; i < prefab.skins.size(); ++i) {
    const std::string skin_key = childKey(key, "skins", i);
    if (!assets.registerSkin(skin_key, prefab.skins[i])) {
      return {};
    }
    asset.skin_keys.push_back(skin_key);
  }
  logSourceImportDiag("gltf_scene",
                      key,
                      path,
                      "register skins",
                      stage_start,
                      core::SteadyClock::now(),
                      asset.skin_keys.size());

  stage_start = core::SteadyClock::now();
  if (!assets.registerGltfSceneAsset(key, asset)) {
    return {};
  }
  logSourceImportDiag("gltf_scene", key, path, "register scene", stage_start, core::SteadyClock::now());
  logSourceImportDiag("gltf_scene",
                      key,
                      path,
                      "total",
                      total_start,
                      core::SteadyClock::now(),
                      asset.nodes.size());
  return asset;
}

}  // namespace detail

}  // namespace karma::assets
