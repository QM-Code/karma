#include "karma/scenes.h"

#include "scene_runtime_assets.h"
#include "scene_runtime_prefabs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/foliage.h"
#include "karma/prefabs.h"

namespace karma::scenes {

namespace {

uint64_t entityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
}

std::optional<std::string> sceneEntityReferenceId(
    const nlohmann::json& reference) {
  if (reference.is_string()) {
    const std::string id = reference.get<std::string>();
    return id.empty() ? std::nullopt
                      : std::optional<std::string>(id);
  }
  if (!reference.is_object()) return std::nullopt;
  const auto scope_it = reference.find("scope");
  if (scope_it != reference.end() &&
      (!scope_it->is_string() ||
       scope_it->get<std::string>() != "scene")) {
    return std::nullopt;
  }
  auto id_it = reference.find("id");
  if (id_it == reference.end()) {
    id_it = reference.find("scene_entity_id");
  }
  if (id_it == reference.end() || !id_it->is_string()) {
    return std::nullopt;
  }
  const std::string id = id_it->get<std::string>();
  return id.empty() ? std::nullopt
                    : std::optional<std::string>(id);
}

components::TransformComponent toTransform(const SceneTransform& transform) {
  return components::TransformComponent{
      transform.position,
      transform.rotation,
      transform.scale,
  };
}

SceneTransform localSceneTransform(const components::TransformComponent& transform) {
  return SceneTransform{
      .position = transform.localPosition(),
      .rotation = transform.localRotation(),
      .scale = transform.localScale(),
  };
}

SceneTransform worldSceneTransform(const components::TransformComponent& transform) {
  return SceneTransform{
      .position = transform.worldPosition(),
      .rotation = transform.worldRotation(),
      .scale = transform.worldScale(),
  };
}

void resolveAuthoredComponentPaths(world::World& world,
                                   world::Entity entity,
                                   const SceneDocument& document,
                                   const std::filesystem::path& reference_root) {
  auto resolve = [&](std::filesystem::path& path) {
    path = detail::resolveDocumentPath(document, path, reference_root);
  };

  if (world.has<components::TerrainComponent>(entity)) {
    auto& terrain = world.get<components::TerrainComponent>(entity);
    resolve(terrain.tile_directory);
    resolve(terrain.height_image);
    resolve(terrain.heatmap_image);
    resolve(terrain.color_image);
    resolve(terrain.control_image);
    for (components::TerrainMaterialLayer& layer : terrain.material_layers) {
      resolve(layer.albedo_image);
      resolve(layer.normal_image);
      resolve(layer.roughness_image);
    }
    for (components::TerrainDataMapBinding& map : terrain.data_maps) {
      resolve(map.image);
    }
  }

  if (world.has<components::FoliageComponent>(entity)) {
    resolve(world.get<components::FoliageComponent>(entity).sidecar_path);
  }
}

math::Vec3 minVec(const math::Vec3& a, const math::Vec3& b) {
  return {
      std::min(a.x, b.x),
      std::min(a.y, b.y),
      std::min(a.z, b.z),
  };
}

math::Vec3 maxVec(const math::Vec3& a, const math::Vec3& b) {
  return {
      std::max(a.x, b.x),
      std::max(a.y, b.y),
      std::max(a.z, b.z),
  };
}

math::Vec3 transformPoint(const SceneTransform& transform, const math::Vec3& point) {
  const math::Vec3 scaled = math::multiply(point, transform.scale);
  return math::add(transform.position, math::rotateVec(transform.rotation, scaled));
}

std::array<math::Vec3, 8> boundsCorners(const math::Vec3& min, const math::Vec3& max) {
  return {
      math::Vec3{min.x, min.y, min.z},
      math::Vec3{max.x, min.y, min.z},
      math::Vec3{min.x, max.y, min.z},
      math::Vec3{max.x, max.y, min.z},
      math::Vec3{min.x, min.y, max.z},
      math::Vec3{max.x, min.y, max.z},
      math::Vec3{min.x, max.y, max.z},
      math::Vec3{max.x, max.y, max.z},
  };
}

bool computeMeshBounds(const world::MeshData& mesh,
                       const SceneTransform& world_transform,
                       SceneStaticBounds& out_bounds) {
  if (mesh.vertices.empty()) {
    return false;
  }

  math::Vec3 local_min = math::fromGlm(mesh.vertices.front());
  if (!math::isFinite(local_min) || !math::isFinite(world_transform.position) ||
      !math::isFinite(world_transform.rotation) ||
      !math::isFinite(world_transform.scale) ||
      math::lengthSquared(world_transform.rotation) <= 1.0e-12f) {
    return false;
  }
  math::Vec3 local_max = local_min;
  for (const glm::vec3& vertex : mesh.vertices) {
    const math::Vec3 point = math::fromGlm(vertex);
    if (!math::isFinite(point)) {
      return false;
    }
    local_min = minVec(local_min, point);
    local_max = maxVec(local_max, point);
  }

  const std::array<math::Vec3, 8> corners = boundsCorners(local_min, local_max);
  std::array<math::Vec3, 8> world_corners{};
  for (size_t index = 0; index < corners.size(); ++index) {
    world_corners[index] = transformPoint(world_transform, corners[index]);
    if (!math::isFinite(world_corners[index])) {
      return false;
    }
  }

  math::Vec3 world_min = world_corners.front();
  math::Vec3 world_max = world_min;
  for (const math::Vec3& point : world_corners) {
    world_min = minVec(world_min, point);
    world_max = maxVec(world_max, point);
  }

  const math::Vec3 center = math::scale(math::add(world_min, world_max), 0.5f);
  if (!math::isFinite(center)) {
    return false;
  }
  float radius_squared = 0.0f;
  for (const math::Vec3& point : world_corners) {
    const float distance_squared =
        math::lengthSquared(math::subtract(point, center));
    if (!std::isfinite(distance_squared)) {
      return false;
    }
    radius_squared = std::max(radius_squared, distance_squared);
  }
  const float radius = std::sqrt(radius_squared);
  if (!std::isfinite(radius)) {
    return false;
  }

  out_bounds.local_min = local_min;
  out_bounds.local_max = local_max;
  out_bounds.world_min = world_min;
  out_bounds.world_max = world_max;
  out_bounds.world_center = center;
  out_bounds.world_radius = radius;
  return true;
}

bool appendDiagnostic(SceneInstantiateResult& result, std::string message) {
  result.diagnostics.push_back(std::move(message));
  return false;
}

void addStaticMeshComponents(world::World& world,
                             const SceneDocument& document,
                             SceneInstantiateResult& result) {
  for (const SceneStaticComponent& static_component : document.static_components) {
    const auto entity_it = result.entities_by_id.find(static_component.entity_id);
    if (entity_it == result.entities_by_id.end() ||
        !world.isAlive(entity_it->second)) {
      continue;
    }

    uint32_t static_flags = 0u;
    if (static_component.render) {
      static_flags |= components::StaticComponentRender;
    }
    if (static_component.lighting) {
      static_flags |= components::StaticComponentLighting;
    }
    if (static_component.casts_shadows) {
      static_flags |= components::StaticComponentShadows;
    }
    if (static_component.collision) {
      static_flags |= components::StaticComponentCollision;
    }
    if (static_component.navigation) {
      static_flags |= components::StaticComponentNavigation;
    }
    components::StaticComponent runtime_static{
        .enabled = true,
        .include_descendants = false,
        .flags = static_flags,
    };
    if (!world.has<components::StaticComponent>(entity_it->second)) {
      world.add(entity_it->second, runtime_static);
    }

    if (!static_component.render || static_component.mesh_asset_key.empty()) {
      continue;
    }

    components::MeshComponent mesh{};
    mesh.mesh_asset_key = static_component.mesh_asset_key;
    mesh.visible = static_component.render;
    mesh.shadow_visible = static_component.casts_shadows;
    if (!static_component.material_asset_key.empty()) {
      mesh.materials.push_back(components::MeshMaterialAssignment{
          .slot = 0u,
          .material_key = static_component.material_asset_key,
      });
    }
    world.add(entity_it->second, std::move(mesh));
  }
}

void materializeInheritedStaticComponents(world::World& world,
                                          const world::Scene& scene) {
  auto visit = [&](auto&& self,
                   world::NodeId node_id,
                   std::optional<components::StaticComponent> inherited)
      -> void {
    if (!scene.isAlive(node_id)) {
      return;
    }
    const world::Node& node = scene.get(node_id);
    std::optional<components::StaticComponent> descendants;
    if (node.entity.isValid() && world.isAlive(node.entity)) {
      if (world.has<components::StaticComponent>(node.entity)) {
        const auto& explicit_membership =
            world.get<components::StaticComponent>(node.entity);
        if (explicit_membership.enabled &&
            explicit_membership.include_descendants &&
            explicit_membership.flags != 0u) {
          descendants = explicit_membership;
        } else {
          descendants = components::StaticComponent{
              .enabled = false,
              .include_descendants = true,
              .flags = 0u,
          };
        }
      } else if (inherited.has_value()) {
        components::StaticComponent effective = *inherited;
        effective.include_descendants = true;
        world.add(node.entity, effective);
        descendants = effective;
      }
    } else {
      descendants = inherited;
    }
    for (const world::NodeId child : node.children) {
      self(self, child, descendants);
    }
  };

  for (const world::Node& node : scene.nodes()) {
    if (node.id != world::Node::kInvalidId &&
        node.parent == world::Node::kInvalidId) {
      visit(visit, node.id, std::nullopt);
    }
  }
}

bool finiteTransform(const SceneTransform& transform) {
  return math::isFinite(transform.position) && math::isFinite(transform.rotation) &&
         math::isFinite(transform.scale) &&
         math::lengthSquared(transform.rotation) > 1.0e-12f;
}

bool finiteCamera(const components::CameraComponent& camera) {
  return std::isfinite(camera.fov_y_degrees) && std::isfinite(camera.near_clip) &&
         std::isfinite(camera.far_clip) && std::isfinite(camera.ortho_left) &&
         std::isfinite(camera.ortho_right) && std::isfinite(camera.ortho_top) &&
         std::isfinite(camera.ortho_bottom) &&
         std::isfinite(camera.anti_aliasing.ssaa_scale);
}

bool finiteLight(const components::LightComponent& light) {
  return math::isFinite(light.color) && std::isfinite(light.intensity) &&
         std::isfinite(light.range) && std::isfinite(light.inner_cone_degrees) &&
         std::isfinite(light.outer_cone_degrees) &&
         std::isfinite(light.shadow_extent);
}

bool portableRelativePath(const std::filesystem::path& path,
                          bool allow_empty = true) {
  if (path.empty()) return allow_empty;
  if (path.is_absolute() || path.has_root_name()) return false;
  const std::string value = path.generic_string();
  if (value.empty() || value.find('\\') != std::string::npos ||
      value.find('\0') != std::string::npos || value.front() == '/') {
    return false;
  }
  if (value.size() >= 2u &&
      std::isalpha(static_cast<unsigned char>(value[0])) != 0 &&
      value[1] == ':') {
    return false;
  }
  for (const std::filesystem::path& segment : path) {
    if (segment == "..") return false;
  }
  return true;
}

struct RuntimeSceneBakeManifest {
  uint32_t version = 0u;
  std::string scene_fingerprint;
  std::vector<SceneAssetRef> produced_assets;
  std::vector<BakedLightmapBinding> lightmap_bindings;
  std::vector<std::string> mixed_light_ids;
  std::vector<BakedNavigationBinding> navigation_bindings;
};

bool readJsonUint64(const nlohmann::json& value, uint64_t& out) {
  if (value.is_number_unsigned()) {
    out = value.get<uint64_t>();
    return true;
  }
  if (!value.is_number_integer()) return false;
  const int64_t signed_value = value.get<int64_t>();
  if (signed_value < 0) return false;
  out = static_cast<uint64_t>(signed_value);
  return true;
}

const SceneAssetRef* findProducedAsset(
    const RuntimeSceneBakeManifest& manifest,
    std::string_view id) {
  const auto it = std::find_if(
      manifest.produced_assets.begin(),
      manifest.produced_assets.end(),
      [&](const SceneAssetRef& asset) { return asset.id == id; });
  return it == manifest.produced_assets.end() ? nullptr : &*it;
}

bool readRuntimeSceneBakeManifest(const std::filesystem::path& path,
                                  RuntimeSceneBakeManifest& out,
                                  std::string& diagnostic) {
  out = RuntimeSceneBakeManifest{};
  std::ifstream stream(path);
  if (!stream) {
    diagnostic = "failed to open scene bake manifest";
    return false;
  }
  nlohmann::json json;
  try {
    stream >> json;
  } catch (const std::exception& error) {
    diagnostic = std::string("failed to parse scene bake manifest: ") +
                 error.what();
    return false;
  }
  if (!json.is_object() ||
      json.value("schema", std::string{}) != "karma.scene_bake") {
    diagnostic = "invalid scene bake manifest schema";
    return false;
  }
  const auto version_it = json.find("version");
  if (version_it == json.end() ||
      (!version_it->is_number_unsigned() &&
       !version_it->is_number_integer())) {
    diagnostic = "scene bake manifest version must be an integer";
    return false;
  }
  const bool version_one = version_it->is_number_unsigned()
                               ? version_it->get<uint64_t>() == 1u
                               : version_it->get<int64_t>() == 1;
  const bool version_two = version_it->is_number_unsigned()
                               ? version_it->get<uint64_t>() == 2u
                               : version_it->get<int64_t>() == 2;
  if (!version_one && !version_two) {
    diagnostic = "unsupported scene bake manifest version: " +
                 version_it->dump();
    return false;
  }
  out.version = version_one ? 1u : 2u;
  const auto scene_fingerprint_it = json.find("scene_fingerprint");
  if (scene_fingerprint_it != json.end() &&
      !scene_fingerprint_it->is_string()) {
    diagnostic = "scene bake manifest fingerprint must be a string";
    return false;
  }
  if (scene_fingerprint_it != json.end()) {
    out.scene_fingerprint = scene_fingerprint_it->get<std::string>();
  }
  if (version_one) {
    // V1 had only an informational nav_cache_files list and no stable owner
    // binding. Accept it and let source navigation build normally.
    return true;
  }

  if (out.scene_fingerprint.empty()) {
    diagnostic = "scene bake manifest fingerprint must not be empty";
    return false;
  }

  const auto produced_it = json.find("produced_assets");
  if (produced_it != json.end()) {
    if (!produced_it->is_array()) {
      diagnostic = "scene bake produced_assets must be an array";
      return false;
    }
    std::unordered_set<std::string> produced_ids;
    for (const nlohmann::json& asset_json : *produced_it) {
      if (!asset_json.is_object()) {
        diagnostic = "scene bake produced asset must be an object";
        return false;
      }
      const auto id_it = asset_json.find("id");
      const auto path_it = asset_json.find("path");
      const auto type_it = asset_json.find("type");
      if (id_it == asset_json.end() || !id_it->is_string() ||
          id_it->get_ref<const std::string&>().empty() ||
          path_it == asset_json.end() || !path_it->is_string() ||
          type_it == asset_json.end() || !type_it->is_string() ||
          type_it->get_ref<const std::string&>().empty()) {
        diagnostic = "scene bake produced asset fields are invalid";
        return false;
      }
      SceneAssetRef asset{};
      asset.id = id_it->get<std::string>();
      asset.path = path_it->get<std::string>();
      asset.type = type_it->get<std::string>();
      if (!portableRelativePath(asset.path, false)) {
        diagnostic = "scene bake produced asset path is not portable";
        return false;
      }
      if (!produced_ids.insert(asset.id).second) {
        diagnostic = "scene bake has duplicate produced asset id: " +
                     asset.id;
        return false;
      }
      out.produced_assets.push_back(std::move(asset));
    }
  }

  const auto lighting_output_it = json.find("lighting_output");
  if (lighting_output_it != json.end()) {
    if (!lighting_output_it->is_object()) {
      diagnostic = "scene bake lighting_output must be an object";
      return false;
    }
    const auto mixed_it = lighting_output_it->find("mixed_light_ids");
    if (mixed_it != lighting_output_it->end()) {
      if (!mixed_it->is_array()) {
        diagnostic =
            "scene bake lighting_output mixed_light_ids must be an array";
        return false;
      }
      std::unordered_set<std::string> mixed_ids;
      for (const nlohmann::json& id_json : *mixed_it) {
        if (!id_json.is_string() ||
            id_json.get_ref<const std::string&>().empty()) {
          diagnostic = "scene bake Mixed-light id must be a non-empty string";
          return false;
        }
        const std::string id = id_json.get<std::string>();
        if (!mixed_ids.insert(id).second) {
          diagnostic = "scene bake has duplicate Mixed-light id: " + id;
          return false;
        }
        out.mixed_light_ids.push_back(id);
      }
      if (out.mixed_light_ids.size() > 64u) {
        diagnostic = "scene bake supports at most 64 Mixed-light ids";
        return false;
      }
    }
  }

  const auto lightmap_bindings_it = json.find("lightmap_bindings");
  if (lightmap_bindings_it != json.end()) {
    if (!lightmap_bindings_it->is_array()) {
      diagnostic = "scene bake lightmap_bindings must be an array";
      return false;
    }
    std::unordered_set<std::string> targets;
    for (const nlohmann::json& binding_json : *lightmap_bindings_it) {
      if (!binding_json.is_object()) {
        diagnostic = "scene bake lightmap binding must be an object";
        return false;
      }
      const auto target_it = binding_json.find("target_id");
      const auto mesh_it = binding_json.find("derived_mesh_asset_key");
      const auto irradiance_it =
          binding_json.find("irradiance_asset_key");
      const auto uv_it = binding_json.find("uv_scale_offset");
      const auto intensity_it = binding_json.find("intensity");
      const auto mask_it = binding_json.find("mixed_light_mask");
      if (target_it == binding_json.end() || !target_it->is_string() ||
          target_it->get_ref<const std::string&>().empty() ||
          mesh_it == binding_json.end() || !mesh_it->is_string() ||
          mesh_it->get_ref<const std::string&>().empty() ||
          irradiance_it == binding_json.end() ||
          !irradiance_it->is_string() ||
          irradiance_it->get_ref<const std::string&>().empty() ||
          uv_it == binding_json.end() || !uv_it->is_array() ||
          uv_it->size() != 4u || intensity_it == binding_json.end() ||
          !intensity_it->is_number() || mask_it == binding_json.end()) {
        diagnostic = "scene bake lightmap binding fields are invalid";
        return false;
      }
      BakedLightmapBinding binding{};
      binding.target_id = target_it->get<std::string>();
      binding.derived_mesh_asset_key = mesh_it->get<std::string>();
      binding.irradiance_asset_key = irradiance_it->get<std::string>();
      const auto direction_it = binding_json.find("direction_asset_key");
      if (direction_it != binding_json.end()) {
        if (!direction_it->is_string()) {
          diagnostic =
              "scene bake lightmap direction asset key must be a string";
          return false;
        }
        binding.direction_asset_key = direction_it->get<std::string>();
      }
      for (size_t index = 0u; index < binding.uv_scale_offset.size(); ++index) {
        if (!(*uv_it)[index].is_number()) {
          diagnostic = "scene bake lightmap UV transform must be numeric";
          return false;
        }
        const double value = (*uv_it)[index].get<double>();
        if (!std::isfinite(value) ||
            value < -static_cast<double>(std::numeric_limits<float>::max()) ||
            value > static_cast<double>(std::numeric_limits<float>::max())) {
          diagnostic = "scene bake lightmap UV transform must be finite";
          return false;
        }
        binding.uv_scale_offset[index] = static_cast<float>(value);
      }
      const double intensity = intensity_it->get<double>();
      if (!std::isfinite(intensity) || intensity < 0.0 ||
          intensity > static_cast<double>(std::numeric_limits<float>::max())) {
        diagnostic =
            "scene bake lightmap intensity must be finite and non-negative";
        return false;
      }
      binding.intensity = static_cast<float>(intensity);
      if (!readJsonUint64(*mask_it, binding.mixed_light_mask)) {
        diagnostic =
            "scene bake lightmap Mixed-light mask must be a uint64";
        return false;
      }
      const uint64_t allowed_mask =
          out.mixed_light_ids.size() == 64u
              ? std::numeric_limits<uint64_t>::max()
              : (out.mixed_light_ids.empty()
                     ? 0u
                     : ((uint64_t{1} << out.mixed_light_ids.size()) - 1u));
      if ((binding.mixed_light_mask & ~allowed_mask) != 0u) {
        diagnostic =
            "scene bake lightmap mask references an unknown Mixed light";
        return false;
      }
      if (!assets::AssetRegistry::isValidAssetKey(
              binding.derived_mesh_asset_key) ||
          !assets::AssetRegistry::isValidAssetKey(
              binding.irradiance_asset_key) ||
          (!binding.direction_asset_key.empty() &&
           !assets::AssetRegistry::isValidAssetKey(
               binding.direction_asset_key))) {
        diagnostic = "scene bake lightmap binding has an invalid asset key";
        return false;
      }
      if (!targets.insert(binding.target_id).second) {
        diagnostic = "scene bake has duplicate lightmap target: " +
                     binding.target_id;
        return false;
      }
      out.lightmap_bindings.push_back(std::move(binding));
    }
  }

  for (const BakedLightmapBinding& binding : out.lightmap_bindings) {
    const SceneAssetRef* mesh =
        findProducedAsset(out, binding.derived_mesh_asset_key);
    const SceneAssetRef* irradiance =
        findProducedAsset(out, binding.irradiance_asset_key);
    const SceneAssetRef* direction =
        binding.direction_asset_key.empty()
            ? nullptr
            : findProducedAsset(out, binding.direction_asset_key);
    if (mesh == nullptr || mesh->type != "baked_mesh" ||
        mesh->path.extension() != ".kbmesh") {
      diagnostic = "scene bake lightmap binding has no valid derived mesh asset";
      return false;
    }
    if (irradiance == nullptr ||
        irradiance->type != "baked_irradiance_rgba8" ||
        irradiance->path.extension() != ".krgba8") {
      diagnostic =
          "scene bake lightmap binding has no valid irradiance asset";
      return false;
    }
    if (!binding.direction_asset_key.empty() &&
        (direction == nullptr ||
         (direction->type != "baked_direction_rgba8" &&
          direction->type != "baked_irradiance_rgba8") ||
         direction->path.extension() != ".krgba8")) {
      diagnostic = "scene bake lightmap binding has no valid direction asset";
      return false;
    }
  }

  const auto bindings_it = json.find("navigation_bindings");
  if (bindings_it == json.end()) {
    return true;
  }
  if (!bindings_it->is_array()) {
    diagnostic = "scene bake navigation_bindings must be an array";
    return false;
  }
  std::unordered_set<std::string> owners;
  for (const nlohmann::json& binding_json : *bindings_it) {
    if (!binding_json.is_object()) {
      diagnostic = "scene bake navigation binding must be an object";
      return false;
    }
    const auto owner_it = binding_json.find("owner_id");
    const auto kind_it = binding_json.find("kind");
    const auto path_it = binding_json.find("path");
    const auto fingerprint_it = binding_json.find("source_fingerprint");
    if (owner_it == binding_json.end() || !owner_it->is_string() ||
        owner_it->get_ref<const std::string&>().empty() ||
        kind_it == binding_json.end() || !kind_it->is_string() ||
        path_it == binding_json.end() || !path_it->is_string() ||
        fingerprint_it == binding_json.end() ||
        !fingerprint_it->is_string()) {
      diagnostic = "scene bake navigation binding fields are invalid";
      return false;
    }
    BakedNavigationBinding binding{};
    binding.owner_id = owner_it->get<std::string>();
    const std::string kind = kind_it->get<std::string>();
    if (kind == "navmesh") {
      binding.kind = BakedNavigationKind::NavMesh;
    } else if (kind == "tile_cache") {
      binding.kind = BakedNavigationKind::TileCache;
    } else {
      diagnostic = "scene bake navigation binding kind is invalid";
      return false;
    }
    binding.path = path_it->get<std::string>();
    binding.source_fingerprint = fingerprint_it->get<std::string>();
    if (!portableRelativePath(binding.path, false)) {
      diagnostic = "scene bake navigation binding path is not portable";
      return false;
    }
    if (!owners.insert(binding.owner_id).second) {
      diagnostic = "scene bake has duplicate navigation owner binding: " +
                   binding.owner_id;
      return false;
    }
    out.navigation_bindings.push_back(std::move(binding));
  }
  return true;
}

assets::TextureAsset runtimeLightmapTexture(
    assets::Rgba8Image image,
    assets::TextureAsset::Semantic semantic) {
  assets::TextureAsset texture{};
  texture.desc.width = image.width;
  texture.desc.height = image.height;
  texture.desc.format = rendering::TextureFormat::RGBA8;
  texture.desc.srgb = false;
  texture.desc.generate_mips = false;
  texture.desc.mip_levels = 1u;
  texture.payload_format = assets::TextureAsset::PayloadFormat::RGBA8;
  texture.semantic = semantic;
  texture.bytes = std::move(image.pixels);
  texture.subresources.push_back(rendering::TextureUploadSubresource{
      .mip_level = 0u,
      .array_layer = 0u,
      .width = texture.desc.width,
      .height = texture.desc.height,
      .offset = 0u,
      .size = texture.bytes.size(),
      .row_stride = static_cast<size_t>(texture.desc.width) * 4u,
  });
  return texture;
}

bool validRuntimeLightmapMesh(const world::MeshData& mesh) {
  if (mesh.vertices.empty() || mesh.indices.empty() ||
      mesh.indices.size() % 3u != 0u ||
      mesh.uvs1.size() != mesh.vertices.size()) {
    return false;
  }
  for (const glm::vec3& vertex : mesh.vertices) {
    if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
        !std::isfinite(vertex.z)) {
      return false;
    }
  }
  for (const glm::vec2& uv : mesh.uvs1) {
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return false;
  }
  for (const uint32_t index : mesh.indices) {
    if (index >= mesh.vertices.size()) return false;
  }
  for (const world::MeshSubmesh& submesh : mesh.submeshes) {
    if (submesh.index_offset > mesh.indices.size() ||
        submesh.index_count > mesh.indices.size() - submesh.index_offset) {
      return false;
    }
  }
  return true;
}

class RuntimeGeneratedAssetTransaction {
 public:
  RuntimeGeneratedAssetTransaction(assets::AssetRegistry& assets,
                                   size_t mesh_count,
                                   size_t texture_count,
                                   size_t material_count)
      : assets_(assets) {
    mesh_keys_.reserve(mesh_count);
    texture_keys_.reserve(texture_count);
    material_keys_.reserve(material_count);
  }

  ~RuntimeGeneratedAssetTransaction() {
    if (committed_) return;
    for (auto it = material_keys_.rbegin(); it != material_keys_.rend(); ++it) {
      assets_.unregisterMaterial(*it);
    }
    for (auto it = texture_keys_.rbegin(); it != texture_keys_.rend(); ++it) {
      assets_.unregisterTextureAsset(*it);
    }
    for (auto it = mesh_keys_.rbegin(); it != mesh_keys_.rend(); ++it) {
      assets_.unregisterMeshAsset(*it);
    }
  }

  bool registerMesh(std::string key, world::MeshData mesh) {
    try {
      if (!assets_.registerMeshAsset(key, std::move(mesh))) return false;
      mesh_keys_.push_back(std::move(key));
      return true;
    } catch (...) {
      assets_.unregisterMeshAsset(key);
      throw;
    }
  }

  bool registerTexture(std::string key, assets::TextureAsset texture) {
    try {
      if (!assets_.registerTextureAsset(key, std::move(texture))) return false;
      texture_keys_.push_back(std::move(key));
      return true;
    } catch (...) {
      assets_.unregisterTextureAsset(key);
      throw;
    }
  }

  bool registerMaterial(std::string key,
                        rendering::MaterialVariantDesc material) {
    try {
      if (!assets_.registerMaterialVariant(key, std::move(material))) {
        return false;
      }
      material_keys_.push_back(std::move(key));
      return true;
    } catch (...) {
      assets_.unregisterMaterial(key);
      throw;
    }
  }

  bool registerMaterial(std::string key,
                        rendering::MaterialAssetDesc material) {
    try {
      if (!assets_.registerMaterialAsset(key, std::move(material))) {
        return false;
      }
      material_keys_.push_back(std::move(key));
      return true;
    } catch (...) {
      assets_.unregisterMaterial(key);
      throw;
    }
  }

  void commit(SceneInstantiateResult& result) {
    for (std::string& key : mesh_keys_) {
      result.generated_mesh_asset_keys.push_back(std::move(key));
    }
    for (std::string& key : texture_keys_) {
      result.generated_texture_asset_keys.push_back(std::move(key));
    }
    for (std::string& key : material_keys_) {
      result.generated_material_asset_keys.push_back(std::move(key));
    }
    committed_ = true;
  }

 private:
  assets::AssetRegistry& assets_;
  std::vector<std::string> mesh_keys_;
  std::vector<std::string> texture_keys_;
  std::vector<std::string> material_keys_;
  bool committed_ = false;
};

bool currentBakeFingerprintMatches(const SceneDocument& document,
                                   const SceneBakeDesc& bake,
                                   const RuntimeSceneBakeManifest& manifest,
                                   std::string& diagnostic) {
  try {
    const std::string current = sceneBakeFingerprint(document, bake);
    if (current.empty() || current != manifest.scene_fingerprint) {
      diagnostic = "scene bake manifest fingerprint is stale";
      return false;
    }
  } catch (const std::exception& error) {
    diagnostic = std::string("failed to compute current scene bake fingerprint: ") +
                 error.what();
    return false;
  } catch (...) {
    diagnostic = "failed to compute current scene bake fingerprint";
    return false;
  }
  return true;
}

std::optional<world::Entity> resolveMixedLightEntity(
    const SceneInstantiateResult& result,
    std::string_view stable_id) {
  constexpr std::string_view kSceneLightPrefix = "scene_light:";
  constexpr std::string_view kOwnerPrefix = "owner:";
  if (stable_id.starts_with(kSceneLightPrefix)) {
    const auto it = result.lights_by_id.find(
        std::string(stable_id.substr(kSceneLightPrefix.size())));
    if (it != result.lights_by_id.end()) return it->second;
  } else if (stable_id.starts_with(kOwnerPrefix)) {
    const auto it = result.navigation_owners_by_id.find(
        std::string(stable_id.substr(kOwnerPrefix.size())));
    if (it != result.navigation_owners_by_id.end()) return it->second;
  }
  return std::nullopt;
}

void applyRuntimeLightmapBindings(
    world::World& world,
    const SceneDocument& document,
    const SceneBakeDesc& bake,
    const std::filesystem::path& reference_root,
    assets::AssetRegistry& assets,
    SceneInstantiateResult& result) {
  if (!bake.enabled || !bake.load_at_runtime || !bake.lighting.enabled ||
      bake.path.empty()) {
    return;
  }
  const std::filesystem::path manifest_path =
      detail::resolveDocumentPath(document, bake.path, reference_root);
  if (!std::filesystem::exists(manifest_path)) {
    result.diagnostics.push_back(
        "scene bake '" + bake.id +
        "' lightmaps ignored: manifest is missing; using source rendering");
    return;
  }

  RuntimeSceneBakeManifest manifest{};
  std::string diagnostic;
  if (!readRuntimeSceneBakeManifest(manifest_path, manifest, diagnostic)) {
    result.diagnostics.push_back("scene bake '" + bake.id +
                                 "' lightmaps ignored: " + diagnostic +
                                 "; using source rendering");
    return;
  }
  if (manifest.version == 1u || manifest.lightmap_bindings.empty()) return;
  if (!currentBakeFingerprintMatches(document, bake, manifest, diagnostic)) {
    result.diagnostics.push_back("scene bake '" + bake.id +
                                 "' lightmaps ignored: " + diagnostic +
                                 "; using source rendering");
    return;
  }

  struct PreparedMesh {
    std::string key;
    world::MeshData mesh;
  };
  struct PreparedTexture {
    std::string key;
    assets::TextureAsset texture;
  };
  struct PreparedMaterial {
    std::string key;
    std::optional<rendering::MaterialAssetDesc> asset;
    std::optional<rendering::MaterialVariantDesc> variant;
  };
  struct PreparedTarget {
    world::Entity entity{};
    std::string mesh_key;
    std::vector<components::MeshMaterialAssignment> materials;
  };

  std::vector<PreparedMesh> meshes;
  std::vector<PreparedTexture> textures;
  std::vector<PreparedMaterial> materials;
  std::vector<PreparedTarget> targets;
  std::vector<std::pair<world::Entity, uint32_t>> mixed_lights;
  std::unordered_set<std::string> loaded_meshes;
  std::unordered_set<std::string> loaded_textures;
  std::unordered_set<std::string> generated_materials;

  const auto reject = [&](std::string reason) {
    result.diagnostics.push_back("scene bake '" + bake.id +
                                 "' lightmaps ignored: " +
                                 std::move(reason) +
                                 "; using source rendering");
  };

  mixed_lights.reserve(manifest.mixed_light_ids.size());
  for (size_t index = 0u; index < manifest.mixed_light_ids.size(); ++index) {
    const std::string& stable_id = manifest.mixed_light_ids[index];
    const std::optional<world::Entity> entity =
        resolveMixedLightEntity(result, stable_id);
    if (!entity.has_value() || !world.isAlive(*entity) ||
        !world.has<components::LightComponent>(*entity) ||
        world.get<components::LightComponent>(*entity).bake_mode !=
            components::LightComponent::BakeMode::Mixed) {
      reject("Mixed-light owner is unavailable or no longer Mixed: " +
             stable_id);
      return;
    }
    mixed_lights.emplace_back(*entity, static_cast<uint32_t>(index));
  }

  auto load_mesh = [&](const std::string& key) -> bool {
    if (!loaded_meshes.insert(key).second) return true;
    if (assets.findMeshAsset(key) != nullptr) {
      diagnostic = "generated mesh key already exists: " + key;
      return false;
    }
    const SceneAssetRef* produced = findProducedAsset(manifest, key);
    if (produced == nullptr) {
      diagnostic = "derived mesh is not declared in produced_assets: " + key;
      return false;
    }
    std::string artifact_diagnostic;
    std::optional<world::MeshData> mesh = assets::loadBakedMeshArtifact(
        detail::resolveDocumentPath(document, produced->path, reference_root),
        &artifact_diagnostic);
    if (!mesh.has_value() || !validRuntimeLightmapMesh(*mesh)) {
      diagnostic = "failed to load derived mesh artifact '" +
                   produced->path.generic_string() + "': " +
                   (artifact_diagnostic.empty() ? "invalid lightmap mesh"
                                                : artifact_diagnostic);
      return false;
    }
    meshes.push_back(PreparedMesh{.key = key, .mesh = std::move(*mesh)});
    return true;
  };

  auto load_texture = [&](const std::string& key) -> bool {
    if (key.empty() || !loaded_textures.insert(key).second) return true;
    if (assets.findTextureAsset(key) != nullptr) {
      diagnostic = "generated texture key already exists: " + key;
      return false;
    }
    const SceneAssetRef* produced = findProducedAsset(manifest, key);
    if (produced == nullptr) {
      diagnostic = "lightmap texture is not declared in produced_assets: " +
                   key;
      return false;
    }
    std::string artifact_diagnostic;
    std::optional<assets::Rgba8Image> image = assets::loadBakedRgba8Artifact(
        detail::resolveDocumentPath(document, produced->path, reference_root),
        &artifact_diagnostic);
    if (!image.has_value() || !image->valid()) {
      diagnostic = "failed to load lightmap texture artifact '" +
                   produced->path.generic_string() + "': " +
                   (artifact_diagnostic.empty() ? "invalid RGBA8 lightmap"
                                                : artifact_diagnostic);
      return false;
    }
    textures.push_back(PreparedTexture{
        .key = key,
        .texture = runtimeLightmapTexture(
            std::move(*image),
            produced->type == "baked_direction_rgba8"
                ? assets::TextureAsset::Semantic::Data
                : assets::TextureAsset::Semantic::Linear),
    });
    return true;
  };

  targets.reserve(manifest.lightmap_bindings.size());
  for (const BakedLightmapBinding& binding : manifest.lightmap_bindings) {
    const auto owner_it = result.navigation_owners_by_id.find(binding.target_id);
    if (owner_it == result.navigation_owners_by_id.end() ||
        !world.isAlive(owner_it->second) ||
        !world.has<components::MeshComponent>(owner_it->second)) {
      reject("lightmap target is unavailable: " + binding.target_id);
      return;
    }
    const auto& source_component =
        world.get<components::MeshComponent>(owner_it->second);
    const world::MeshData* source_mesh =
        assets.findMeshAsset(source_component.mesh_asset_key);
    if (source_mesh == nullptr) {
      reject("lightmap target source mesh is unavailable: " +
             binding.target_id);
      return;
    }
    if (!load_mesh(binding.derived_mesh_asset_key) ||
        !load_texture(binding.irradiance_asset_key) ||
        !load_texture(binding.direction_asset_key)) {
      reject(diagnostic);
      return;
    }
    const auto derived_mesh_it = std::find_if(
        meshes.begin(), meshes.end(), [&](const PreparedMesh& prepared) {
          return prepared.key == binding.derived_mesh_asset_key;
        });
    if (derived_mesh_it == meshes.end()) {
      reject("loaded lightmap mesh could not be staged: " +
             binding.derived_mesh_asset_key);
      return;
    }
    const world::MeshData& derived_mesh = derived_mesh_it->mesh;

    PreparedTarget target{
        .entity = owner_it->second,
        .mesh_key = binding.derived_mesh_asset_key,
    };
    std::vector<uint32_t> slots;
    slots.reserve(source_mesh->material_slots.size() +
                  source_mesh->submeshes.size() +
                  derived_mesh.material_slots.size() +
                  derived_mesh.submeshes.size() +
                  source_component.materials.size());
    for (size_t slot = 0u; slot < source_mesh->material_slots.size(); ++slot) {
      if (slot > std::numeric_limits<uint32_t>::max()) {
        reject("source mesh has too many material slots: " +
               binding.target_id);
        return;
      }
      slots.push_back(static_cast<uint32_t>(slot));
    }
    for (const world::MeshSubmesh& submesh : source_mesh->submeshes) {
      slots.push_back(submesh.material_slot);
    }
    for (size_t slot = 0u; slot < derived_mesh.material_slots.size(); ++slot) {
      if (slot > std::numeric_limits<uint32_t>::max()) {
        reject("derived mesh has too many material slots: " +
               binding.target_id);
        return;
      }
      slots.push_back(static_cast<uint32_t>(slot));
    }
    for (const world::MeshSubmesh& submesh : derived_mesh.submeshes) {
      slots.push_back(submesh.material_slot);
    }
    for (const components::MeshMaterialAssignment& assignment :
         source_component.materials) {
      slots.push_back(assignment.slot);
    }
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    if (slots.empty()) slots.push_back(0u);

    const std::string target_material_prefix =
        binding.derived_mesh_asset_key + "/lightmap/" +
        assets::hashString(binding.target_id).substr(0u, 16u);
    std::string generated_default_material_key;

    for (const uint32_t slot : slots) {
      std::string base_material_key;
      const auto assigned = std::find_if(
          source_component.materials.begin(),
          source_component.materials.end(),
          [&](const components::MeshMaterialAssignment& assignment) {
            return assignment.slot == slot && !assignment.material_key.empty();
          });
      if (assigned != source_component.materials.end()) {
        base_material_key = assigned->material_key;
      } else if (slot < source_mesh->material_slots.size()) {
        base_material_key =
            source_mesh->material_slots[slot].default_material_key;
      } else if (slot < derived_mesh.material_slots.size()) {
        base_material_key =
            derived_mesh.material_slots[slot].default_material_key;
      }
      if (base_material_key.empty()) {
        if (generated_default_material_key.empty()) {
          generated_default_material_key =
              target_material_prefix + "/default-base";
          if (!assets::AssetRegistry::isValidAssetKey(
                  generated_default_material_key) ||
              assets.findMaterialAsset(generated_default_material_key) !=
                  nullptr ||
              assets.findMaterialVariant(generated_default_material_key) !=
                  nullptr ||
              !generated_materials.insert(generated_default_material_key)
                   .second) {
            reject("generated default lightmap material key is unavailable: " +
                   generated_default_material_key);
            return;
          }
          materials.push_back(PreparedMaterial{
              .key = generated_default_material_key,
              .asset = rendering::MaterialAssetDesc{},
          });
        }
        base_material_key = generated_default_material_key;
      } else if (!assets.resolveMaterial(base_material_key).has_value()) {
        reject("lightmap target material is unavailable: " +
               base_material_key);
        return;
      }

      const std::string variant_key =
          target_material_prefix + "/slot-" + std::to_string(slot);
      if (!assets::AssetRegistry::isValidAssetKey(variant_key) ||
          assets.findMaterialAsset(variant_key) != nullptr ||
          assets.findMaterialVariant(variant_key) != nullptr ||
          !generated_materials.insert(variant_key).second) {
        reject("generated lightmap material key is unavailable: " +
               variant_key);
        return;
      }
      rendering::MaterialVariantDesc variant{};
      variant.base_material_key = std::move(base_material_key);
      variant.params["lightmap_enabled"] = true;
      variant.params["lightmap_intensity"] = binding.intensity;
      variant.params["lightmap_uv_scale_offset"] = glm::vec4{
          binding.uv_scale_offset[0],
          binding.uv_scale_offset[1],
          binding.uv_scale_offset[2],
          binding.uv_scale_offset[3],
      };
      variant.params["lightmap_mixed_mask_low"] =
          static_cast<uint32_t>(binding.mixed_light_mask & 0xffffffffu);
      variant.params["lightmap_mixed_mask_high"] =
          static_cast<uint32_t>(binding.mixed_light_mask >> 32u);
      variant.textures["lightmap"] = binding.irradiance_asset_key;
      if (!binding.direction_asset_key.empty()) {
        variant.textures["lightmap_direction"] =
            binding.direction_asset_key;
      }
      materials.push_back(PreparedMaterial{
          .key = variant_key,
          .variant = std::move(variant),
      });
      target.materials.push_back(components::MeshMaterialAssignment{
          .slot = slot,
          .material_key = variant_key,
      });
    }
    targets.push_back(std::move(target));
  }

  result.generated_mesh_asset_keys.reserve(
      result.generated_mesh_asset_keys.size() + meshes.size());
  result.generated_texture_asset_keys.reserve(
      result.generated_texture_asset_keys.size() + textures.size());
  result.generated_material_asset_keys.reserve(
      result.generated_material_asset_keys.size() + materials.size());
  RuntimeGeneratedAssetTransaction transaction(
      assets, meshes.size(), textures.size(), materials.size());
  for (PreparedMesh& mesh : meshes) {
    if (!transaction.registerMesh(std::move(mesh.key), std::move(mesh.mesh))) {
      reject("failed to register derived lightmap mesh");
      return;
    }
  }
  for (PreparedTexture& texture : textures) {
    if (!transaction.registerTexture(std::move(texture.key),
                                     std::move(texture.texture))) {
      reject("failed to register lightmap texture");
      return;
    }
  }
  for (PreparedMaterial& material : materials) {
    const bool registered = material.asset.has_value()
                                ? transaction.registerMaterial(
                                      std::move(material.key),
                                      std::move(*material.asset))
                                : transaction.registerMaterial(
                                      std::move(material.key),
                                      std::move(*material.variant));
    if (!registered) {
      reject("failed to register lightmap material variant");
      return;
    }
  }
  transaction.commit(result);

  for (PreparedTarget& target : targets) {
    auto& component = world.get<components::MeshComponent>(target.entity);
    component.mesh_asset_key = std::move(target.mesh_key);
    component.materials = std::move(target.materials);
  }
  for (const auto& [entity, bit] : mixed_lights) {
    world.get<components::LightComponent>(entity).mixed_bake_mask_bit = bit;
  }
  for (const world::Entity entity : result.entities) {
    if (!world.isAlive(entity) ||
        !world.has<components::LightComponent>(entity)) {
      continue;
    }
    auto& light = world.get<components::LightComponent>(entity);
    if (light.bake_mode == components::LightComponent::BakeMode::Baked) {
      light.intensity = 0.0f;
    }
  }
}

#if defined(KARMA_ENABLE_NAVIGATION)
void queueNavigationSourceFallback(world::World& world,
                                   world::Entity owner) {
  if (!world.isAlive(owner) ||
      !world.has<components::NavMeshComponent>(owner)) {
    return;
  }
  auto& nav_mesh = world.get<components::NavMeshComponent>(owner);
  nav_mesh.built = false;
  nav_mesh.rebuild_requested = true;
  if (world.has<components::NavTileCacheComponent>(owner)) {
    auto& tile_cache = world.get<components::NavTileCacheComponent>(owner);
    tile_cache.built = false;
    tile_cache.rebuild_requested = true;
  }
}

void applyRuntimeNavigationBindings(
    world::World& world,
    const SceneDocument& document,
    const SceneBakeDesc& bake,
    const std::filesystem::path& reference_root,
    SceneInstantiateResult& result) {
  if (!bake.enabled || !bake.load_at_runtime || !bake.navigation.enabled ||
      bake.path.empty()) {
    return;
  }
  const std::filesystem::path manifest_path =
      detail::resolveDocumentPath(document, bake.path, reference_root);
  if (!std::filesystem::exists(manifest_path)) {
    for (const auto& [owner_id, owner] : result.navigation_owners_by_id) {
      (void)owner_id;
      queueNavigationSourceFallback(world, owner);
    }
    return;
  }

  RuntimeSceneBakeManifest manifest{};
  std::string diagnostic;
  if (!readRuntimeSceneBakeManifest(manifest_path, manifest, diagnostic)) {
    for (const auto& [owner_id, owner] : result.navigation_owners_by_id) {
      (void)owner_id;
      queueNavigationSourceFallback(world, owner);
    }
    result.diagnostics.push_back("scene bake '" + bake.id + "' ignored: " +
                                 diagnostic + "; using source navigation");
    return;
  }
  if (manifest.version == 1u) {
    for (const auto& [owner_id, owner] : result.navigation_owners_by_id) {
      (void)owner_id;
      queueNavigationSourceFallback(world, owner);
    }
    return;
  }
  if (!currentBakeFingerprintMatches(document, bake, manifest, diagnostic)) {
    for (const auto& [owner_id, owner] : result.navigation_owners_by_id) {
      (void)owner_id;
      queueNavigationSourceFallback(world, owner);
    }
    result.diagnostics.push_back("scene bake '" + bake.id +
                                 "' navigation ignored: " + diagnostic +
                                 "; using source navigation");
    return;
  }
  for (const BakedNavigationBinding& binding :
       manifest.navigation_bindings) {
    const auto owner_it =
        result.navigation_owners_by_id.find(binding.owner_id);
    if (owner_it == result.navigation_owners_by_id.end() ||
        !world.isAlive(owner_it->second) ||
        !world.has<components::NavMeshComponent>(owner_it->second)) {
      result.diagnostics.push_back(
          "scene bake navigation owner is unavailable: " + binding.owner_id);
      continue;
    }
    const world::Entity owner = owner_it->second;
    const std::string expected_source_fingerprint = assets::hashString(
        manifest.scene_fingerprint + "\n" + binding.owner_id + "\n" +
        (binding.kind == BakedNavigationKind::TileCache ? "tile_cache"
                                                        : "navmesh"));
    if (manifest.scene_fingerprint.empty() ||
        binding.source_fingerprint != expected_source_fingerprint) {
      queueNavigationSourceFallback(world, owner);
      result.diagnostics.push_back(
          "scene bake navigation binding fingerprint is stale for '" +
          binding.owner_id + "'; using source navigation");
      continue;
    }
    const std::filesystem::path artifact_path =
        detail::resolveDocumentPath(document, binding.path, reference_root);
    bool loaded = false;
    if (binding.kind == BakedNavigationKind::NavMesh) {
      navigation::NavMeshSnapshot snapshot =
          assets::loadNavMeshSnapshot(artifact_path);
      navigation::NavMeshBuildResult build_result{};
      auto& component = world.get<components::NavMeshComponent>(owner);
      loaded = snapshot.valid() &&
               component.nav_mesh.loadSnapshot(snapshot, &build_result);
      if (loaded) {
        component.last_build_result = build_result;
        component.built = true;
        component.rebuild_requested = false;
        component.build_debug_draw_requested = false;
      }
    } else if (world.has<components::NavTileCacheComponent>(owner)) {
      navigation::NavTileCacheSnapshot snapshot =
          assets::loadNavTileCacheSnapshot(artifact_path);
      navigation::NavTileCacheBuildResult cache_result{};
      auto& nav_mesh = world.get<components::NavMeshComponent>(owner);
      auto& tile_cache = world.get<components::NavTileCacheComponent>(owner);
      loaded = snapshot.valid() && tile_cache.tile_cache.loadSnapshot(
                                       nav_mesh.nav_mesh,
                                       snapshot,
                                       &cache_result);
      if (loaded) {
        tile_cache.last_build_result = cache_result;
        tile_cache.built = true;
        tile_cache.rebuild_requested = false;
        tile_cache.updates_pending = false;
        nav_mesh.last_build_result = nav_mesh.nav_mesh.lastBuildResult();
        nav_mesh.built = nav_mesh.nav_mesh.isValid();
        nav_mesh.rebuild_requested = false;
        nav_mesh.build_debug_draw_requested = false;
      }
    }
    if (!loaded) {
      queueNavigationSourceFallback(world, owner);
      result.diagnostics.push_back(
          "scene bake navigation artifact failed to load for '" +
          binding.owner_id + "' from '" + binding.path.generic_string() +
          "'; using source navigation");
    }
  }
}
#endif

}  // namespace

SceneValidationResult validateSceneDocument(const SceneDocument& document) {
  SceneValidationResult result{};
  auto diagnose = [&](std::string message) {
    result.diagnostics.push_back(std::move(message));
  };

  if (document.version != kSceneDocumentVersion) {
    diagnose("unsupported scene document version: " +
             std::to_string(document.version));
  }

  std::unordered_set<std::string> all_ids;
  std::unordered_set<std::string> asset_package_ids;
  std::unordered_set<std::string> gltf_scene_ids;
  std::unordered_set<std::string> entity_ids;
  std::unordered_set<std::string> static_ids;

  auto register_id = [&](std::string_view kind,
                         const std::string& id,
                         std::unordered_set<std::string>* typed_ids,
                         bool required = true) {
    if (id.empty()) {
      if (required) {
        diagnose(std::string(kind) + " id must not be empty");
      }
      return;
    }
    if (!all_ids.insert(id).second) {
      diagnose("duplicate scene id: " + id);
      return;
    }
    if (typed_ids != nullptr) {
      typed_ids->insert(id);
    }
  };

  for (const SceneAssetRef& package : document.asset_packages) {
    register_id("asset package", package.id, &asset_package_ids);
    if (!portableRelativePath(package.path, false) ||
        !portableRelativePath(package.baked_cache_path)) {
      diagnose("asset package '" + package.id +
               "' paths must be portable and content-root relative");
    }
  }
  for (const SceneAssetRef& gltf_scene : document.gltf_scenes) {
    register_id("glTF scene", gltf_scene.id, &gltf_scene_ids);
    if (!portableRelativePath(gltf_scene.path, false) ||
        !portableRelativePath(gltf_scene.baked_cache_path)) {
      diagnose("glTF scene '" + gltf_scene.id +
               "' paths must be portable and content-root relative");
    }
  }
  for (const ScenePrefabInstance& prefab : document.prefab_instances) {
    register_id("prefab instance", prefab.id, nullptr);
    if (prefab.prefab_path.empty()) {
      diagnose("prefab instance '" + prefab.id + "' path must not be empty");
    } else if (!portableRelativePath(prefab.prefab_path, false)) {
      diagnose("prefab instance '" + prefab.id +
               "' path must be portable and content-root relative");
    }
    if (!prefab.variables.is_object()) {
      diagnose("prefab instance '" + prefab.id + "' variables must be an object");
    }
    if (prefab.static_component.has_value() &&
        !components::validStaticComponentFlags(
            prefab.static_component->flags)) {
      diagnose("prefab instance '" + prefab.id +
               "' static flags contain unsupported bits");
    }
    if (!finiteTransform(prefab.transform)) {
      diagnose("prefab instance '" + prefab.id + "' has an invalid transform");
    }
  }
  for (const SceneEntity& entity : document.entities) {
    register_id("entity", entity.id, &entity_ids);
    if (!entity.components.is_object()) {
      diagnose("scene entity '" + entity.id + "' components must be an object");
    }
    if (!finiteTransform(entity.transform)) {
      diagnose("scene entity '" + entity.id + "' has an invalid transform");
    }
  }
  try {
    world::World validation_world;
    std::unordered_map<std::string, world::Entity> validation_entities;
    validation_entities.reserve(document.entities.size());
    for (const SceneEntity& entity : document.entities) {
      const world::Entity validation_entity = validation_world.createEntity();
      validation_world.add(validation_entity, toTransform(entity.transform));
      validation_entities.emplace(entity.id, validation_entity);
    }
    const prefabs::ComponentSerializationContext component_context{
        .resolve_entity_reference =
            [&](const nlohmann::json& reference)
                -> std::optional<world::Entity> {
          const std::optional<std::string> id =
              sceneEntityReferenceId(reference);
          if (!id.has_value()) return std::nullopt;
          const auto it = validation_entities.find(*id);
          return it == validation_entities.end()
                     ? std::nullopt
                     : std::optional<world::Entity>(it->second);
        },
    };
    for (const SceneEntity& entity : document.entities) {
      if (!entity.components.is_object() || entity.components.empty()) continue;
      const auto runtime_it = validation_entities.find(entity.id);
      if (runtime_it == validation_entities.end()) continue;
      SceneInstantiateResult component_validation{};
      if (!detail::deserializeAuthoredComponents(validation_world,
                                                 runtime_it->second,
                                                 entity.components,
                                                 component_validation,
                                                 component_context)) {
        if (component_validation.diagnostics.empty()) {
          diagnose("scene entity '" + entity.id +
                   "' has invalid authored components");
        } else {
          for (const std::string& entry : component_validation.diagnostics) {
            diagnose("scene entity '" + entity.id + "': " + entry);
          }
        }
      }
    }
  } catch (const std::exception& error) {
    diagnose(std::string("scene component validation failed: ") + error.what());
  }
  if (document.environment.has_value()) {
    register_id("environment", document.environment->id, nullptr, false);
    if (!portableRelativePath(document.environment->environment_map_path)) {
      diagnose("scene environment map path must be portable and "
               "content-root relative");
    }
    if (!std::isfinite(document.environment->component.intensity) ||
        document.environment->component.intensity < 0.0f) {
      diagnose("scene environment intensity must be finite and non-negative");
    }
  }
  for (const SceneCamera& camera : document.cameras) {
    register_id("camera", camera.id, nullptr);
    if (!portableRelativePath(camera.component.shader_override_vertex_path) ||
        !portableRelativePath(
            camera.component.shader_override_fragment_path)) {
      diagnose("scene camera '" + camera.id +
               "' shader paths must be portable and content-root relative");
    }
    if (!finiteCamera(camera.component)) {
      diagnose("scene camera '" + camera.id + "' has non-finite parameters");
    }
    if (camera.component.near_clip <= 0.0f ||
        camera.component.far_clip <= camera.component.near_clip) {
      diagnose("scene camera '" + camera.id +
               "' requires 0 < near_clip < far_clip");
    }
    if (camera.component.perspective &&
        (camera.component.fov_y_degrees < 1.0f ||
         camera.component.fov_y_degrees > 179.0f)) {
      diagnose("scene camera '" + camera.id +
               "' perspective FOV must be in [1, 179] degrees");
    }
    if (!camera.component.perspective &&
        (std::abs(camera.component.ortho_right - camera.component.ortho_left) <=
             1.0e-5f ||
         std::abs(camera.component.ortho_top - camera.component.ortho_bottom) <=
             1.0e-5f)) {
      diagnose("scene camera '" + camera.id +
               "' orthographic bounds must have non-zero area");
    }
    switch (camera.component.anti_aliasing.mode) {
      case rendering::AntiAliasingMode::None:
        break;
      case rendering::AntiAliasingMode::MSAA:
        if (camera.component.anti_aliasing.msaa_samples == 0u) {
          diagnose("scene camera '" + camera.id +
                   "' MSAA sample count must be positive");
        }
        break;
      case rendering::AntiAliasingMode::SSAA:
        if (camera.component.anti_aliasing.ssaa_scale < 1.0f ||
            camera.component.anti_aliasing.ssaa_scale > 4.0f) {
          diagnose("scene camera '" + camera.id +
                   "' SSAA scale must be in [1, 4]");
        }
        break;
      default:
        diagnose("scene camera '" + camera.id +
                 "' has an invalid anti-aliasing mode");
        break;
    }
    for (const auto& [name, color] : camera.component.shader_user_params) {
      if (name.empty() || !math::isFinite(color)) {
        diagnose("scene camera '" + camera.id + "' shader parameter '" + name +
                 "' requires a non-empty name and finite color");
      }
    }
  }
  for (const SceneLight& light : document.lights) {
    register_id("light", light.id, nullptr);
    if (!finiteLight(light.component)) {
      diagnose("scene light '" + light.id + "' has non-finite parameters");
    }
    if (light.component.intensity < 0.0f || light.component.range < 0.0f ||
        light.component.shadow_extent < 0.0f) {
      diagnose("scene light '" + light.id +
               "' intensity, range, and shadow extent must be non-negative");
    }
    if (light.component.type == components::LightComponent::Type::Spot &&
        (light.component.inner_cone_degrees < 0.0f ||
         light.component.outer_cone_degrees > 179.0f ||
         light.component.inner_cone_degrees >
             light.component.outer_cone_degrees)) {
      diagnose("scene spot light '" + light.id +
               "' requires 0 <= inner cone <= outer cone <= 179 degrees");
    }
    switch (light.component.type) {
      case components::LightComponent::Type::Directional:
      case components::LightComponent::Type::Point:
      case components::LightComponent::Type::Spot:
        break;
      default:
        diagnose("scene light '" + light.id + "' has an invalid type");
        break;
    }
    switch (light.component.bake_mode) {
      case components::LightComponent::BakeMode::Realtime:
      case components::LightComponent::BakeMode::Mixed:
      case components::LightComponent::BakeMode::Baked:
        break;
      default:
        diagnose("scene light '" + light.id + "' has an invalid bake mode");
        break;
    }
  }
  for (const SceneStaticComponent& component : document.static_components) {
    register_id("static component", component.id, &static_ids);
  }
  for (const SceneBakeDesc& bake : document.bakes) {
    register_id("bake", bake.id, nullptr);
    if (!portableRelativePath(bake.path) ||
        !portableRelativePath(bake.baked_lighting.lightmap_path) ||
        std::any_of(bake.nav_cache_paths.begin(),
                    bake.nav_cache_paths.end(),
                    [](const std::filesystem::path& path) {
                      return !portableRelativePath(path, false);
                    })) {
      diagnose("scene bake '" + bake.id +
               "' paths must be portable and content-root relative");
    }
    if (!std::isfinite(bake.baked_lighting.intensity) ||
        bake.baked_lighting.intensity < 0.0f) {
      diagnose("scene bake '" + bake.id +
               "' lighting intensity must be finite and non-negative");
    }
    if (!std::isfinite(bake.lighting.texels_per_unit) ||
        bake.lighting.texels_per_unit <= 0.0f ||
        bake.lighting.max_atlas_size == 0u ||
        bake.lighting.sky_samples == 0u ||
        !std::isfinite(bake.lighting.ao_max_distance) ||
        bake.lighting.ao_max_distance < 0.0f) {
      diagnose("scene bake '" + bake.id +
               "' has invalid lightmap bake settings");
    }
  }

  auto require_reference = [&](const std::unordered_set<std::string>& ids,
                               std::string_view kind,
                               const std::string& id) {
    if (!id.empty() && ids.find(id) == ids.end()) {
      diagnose("missing " + std::string(kind) + " reference: " + id);
    }
  };

  for (const SceneAssetRef& gltf_scene : document.gltf_scenes) {
    require_reference(asset_package_ids, "asset package", gltf_scene.asset_package_id);
  }
  for (const SceneEntity& entity : document.entities) {
    require_reference(entity_ids, "entity", entity.parent_id);
  }
  for (const ScenePrefabInstance& prefab : document.prefab_instances) {
    require_reference(asset_package_ids, "asset package", prefab.asset_package_id);
    require_reference(entity_ids, "entity", prefab.parent_entity_id);
  }
  if (document.environment.has_value()) {
    require_reference(entity_ids, "entity", document.environment->entity_id);
  }
  for (const SceneCamera& camera : document.cameras) {
    if (camera.entity_id.empty()) {
      diagnose("scene camera '" + camera.id + "' entity reference must not be empty");
    }
    require_reference(entity_ids, "entity", camera.entity_id);
  }
  for (const SceneLight& light : document.lights) {
    if (light.entity_id.empty()) {
      diagnose("scene light '" + light.id + "' entity reference must not be empty");
    }
    require_reference(entity_ids, "entity", light.entity_id);
  }
  for (const SceneStaticComponent& component : document.static_components) {
    if (component.entity_id.empty()) {
      diagnose("static component '" + component.id +
               "' entity reference must not be empty");
    }
    require_reference(entity_ids, "entity", component.entity_id);
    require_reference(gltf_scene_ids, "glTF scene", component.gltf_scene_id);
  }
  for (const SceneBakeDesc& bake : document.bakes) {
    for (const std::string& static_id : bake.static_component_ids) {
      require_reference(static_ids, "static component", static_id);
    }
    require_reference(entity_ids, "entity", bake.baked_lighting.entity_id);
  }

  std::unordered_map<std::string, std::string_view> parents;
  parents.reserve(document.entities.size());
  for (const SceneEntity& entity : document.entities) {
    if (!entity.id.empty()) {
      parents.emplace(entity.id, entity.parent_id);
    }
  }
  std::unordered_map<std::string, uint8_t> visit_state;
  visit_state.reserve(parents.size());
  for (const auto& [start, parent] : parents) {
    (void)parent;
    if (visit_state[start] == 2u) {
      continue;
    }
    std::vector<std::string> path;
    std::string current = start;
    while (!current.empty()) {
      const auto parent_it = parents.find(current);
      if (parent_it == parents.end()) {
        break;
      }
      const uint8_t state = visit_state[current];
      if (state == 2u) {
        break;
      }
      if (state == 1u) {
        diagnose("scene entity hierarchy contains a cycle at: " + current);
        break;
      }
      visit_state[current] = 1u;
      path.push_back(current);
      current = std::string(parent_it->second);
    }
    for (const std::string& id : path) {
      visit_state[id] = 2u;
    }
  }

  return result;
}

SceneInstantiateResult::SceneInstantiateResult(
    SceneInstantiateResult&& other) noexcept
    : success(std::exchange(other.success, false)),
      diagnostics(std::move(other.diagnostics)),
      world_instance_id(std::exchange(other.world_instance_id, 0u)),
      scene_instance_id(std::exchange(other.scene_instance_id, 0u)),
      asset_registry(std::exchange(other.asset_registry, nullptr)),
      asset_packages(std::move(other.asset_packages)),
      prefab_asset_packages(std::move(other.prefab_asset_packages)),
      generated_mesh_asset_keys(
          std::move(other.generated_mesh_asset_keys)),
      generated_texture_asset_keys(
          std::move(other.generated_texture_asset_keys)),
      generated_material_asset_keys(
          std::move(other.generated_material_asset_keys)),
      entities(std::move(other.entities)),
      prefab_roots(std::move(other.prefab_roots)),
      entities_by_id(std::move(other.entities_by_id)),
      gltf_scene_roots_by_id(std::move(other.gltf_scene_roots_by_id)),
      gltf_scene_entities_by_id(
          std::move(other.gltf_scene_entities_by_id)),
      prefab_roots_by_id(std::move(other.prefab_roots_by_id)),
      cameras_by_id(std::move(other.cameras_by_id)),
      lights_by_id(std::move(other.lights_by_id)),
      navigation_owners_by_id(
          std::move(other.navigation_owners_by_id)) {
  other.diagnostics.clear();
  other.asset_packages.clear();
  other.prefab_asset_packages.clear();
  other.generated_mesh_asset_keys.clear();
  other.generated_texture_asset_keys.clear();
  other.generated_material_asset_keys.clear();
  other.entities.clear();
  other.prefab_roots.clear();
  other.entities_by_id.clear();
  other.gltf_scene_roots_by_id.clear();
  other.gltf_scene_entities_by_id.clear();
  other.prefab_roots_by_id.clear();
  other.cameras_by_id.clear();
  other.lights_by_id.clear();
  other.navigation_owners_by_id.clear();
}

SceneInstantiateResult& SceneInstantiateResult::operator=(
    SceneInstantiateResult&& other) {
  if (this == &other) {
    return *this;
  }
  if (!asset_packages.empty() || !prefab_asset_packages.empty() ||
      !generated_mesh_asset_keys.empty() ||
      !generated_texture_asset_keys.empty() ||
      !generated_material_asset_keys.empty() || !entities.empty() ||
      !prefab_roots.empty()) {
    throw std::logic_error(
        "cannot replace a live scene instance; destroyScene must run first");
  }
  success = std::exchange(other.success, false);
  diagnostics = std::move(other.diagnostics);
  world_instance_id = std::exchange(other.world_instance_id, 0u);
  scene_instance_id = std::exchange(other.scene_instance_id, 0u);
  asset_registry = std::exchange(other.asset_registry, nullptr);
  asset_packages = std::move(other.asset_packages);
  prefab_asset_packages = std::move(other.prefab_asset_packages);
  generated_mesh_asset_keys =
      std::move(other.generated_mesh_asset_keys);
  generated_texture_asset_keys =
      std::move(other.generated_texture_asset_keys);
  generated_material_asset_keys =
      std::move(other.generated_material_asset_keys);
  entities = std::move(other.entities);
  prefab_roots = std::move(other.prefab_roots);
  entities_by_id = std::move(other.entities_by_id);
  gltf_scene_roots_by_id = std::move(other.gltf_scene_roots_by_id);
  gltf_scene_entities_by_id = std::move(other.gltf_scene_entities_by_id);
  prefab_roots_by_id = std::move(other.prefab_roots_by_id);
  cameras_by_id = std::move(other.cameras_by_id);
  lights_by_id = std::move(other.lights_by_id);
  navigation_owners_by_id = std::move(other.navigation_owners_by_id);
  other.diagnostics.clear();
  other.asset_packages.clear();
  other.prefab_asset_packages.clear();
  other.generated_mesh_asset_keys.clear();
  other.generated_texture_asset_keys.clear();
  other.generated_material_asset_keys.clear();
  other.entities.clear();
  other.prefab_roots.clear();
  other.entities_by_id.clear();
  other.gltf_scene_roots_by_id.clear();
  other.gltf_scene_entities_by_id.clear();
  other.prefab_roots_by_id.clear();
  other.cameras_by_id.clear();
  other.lights_by_id.clear();
  other.navigation_owners_by_id.clear();
  return *this;
}

world::Entity SceneInstantiateResult::find(std::string_view scene_id) const {
  const std::string key(scene_id);
  if (const auto it = entities_by_id.find(key); it != entities_by_id.end()) {
    return it->second;
  }
  if (const auto it = gltf_scene_roots_by_id.find(key); it != gltf_scene_roots_by_id.end()) {
    return it->second;
  }
  if (const auto it = prefab_roots_by_id.find(key); it != prefab_roots_by_id.end()) {
    return it->second;
  }
  if (const auto it = cameras_by_id.find(key); it != cameras_by_id.end()) {
    return it->second;
  }
  if (const auto it = lights_by_id.find(key); it != lights_by_id.end()) {
    return it->second;
  }
  return {};
}

SceneInstantiateResult instantiateScene(world::World& world,
                                        world::Scene& scene,
                                        assets::AssetRegistry& assets,
                                        const SceneDocument& document,
                                        const SceneInstantiateDesc& desc) {
  SceneInstantiateResult result{};
  result.world_instance_id = world.instanceId();
  result.scene_instance_id = scene.instanceId();
  result.asset_registry = &assets;

  SceneValidationResult validation = validateSceneDocument(document);
  if (!validation.success()) {
    result.diagnostics = std::move(validation.diagnostics);
    return result;
  }

  auto record_entity = [&](world::Entity entity) {
    if (entity.isValid()) result.entities.push_back(entity);
  };

  auto create_recorded_entity = [&] {
    const world::Entity entity = world.createEntity();
    try {
      record_entity(entity);
    } catch (...) {
      world.destroyEntity(entity);
      throw;
    }
    return entity;
  };

  auto rollback_resources = [&] {
    destroyScene(world, scene, result);
    result.success = false;
    result.asset_registry = &assets;
  };

  auto fail_and_rollback = [&](std::string message) {
    // Release ownership before formatting or appending diagnostics: even a
    // reporting allocation failure must not strand entities or package refs.
    rollback_resources();
    try {
      appendDiagnostic(result, std::move(message));
    } catch (...) {
      // The original diagnostic may be lost under memory pressure, but the
      // runtime transaction has already been rolled back.
    }
    return std::move(result);
  };

  try {
    // Perform predictable allocations before acquiring packages or creating
    // runtime objects. Per-import guards below cover variable-sized payloads.
    result.asset_packages.reserve(document.asset_packages.size());
    result.prefab_roots.reserve(document.prefab_instances.size());
    result.prefab_asset_packages.reserve(document.prefab_instances.size());
    result.entities.reserve(document.entities.size());
    result.entities_by_id.reserve(document.entities.size());
    result.gltf_scene_roots_by_id.reserve(document.gltf_scenes.size());
    result.gltf_scene_entities_by_id.reserve(document.gltf_scenes.size());
    result.prefab_roots_by_id.reserve(document.prefab_instances.size());
    result.cameras_by_id.reserve(document.cameras.size());
    result.lights_by_id.reserve(document.lights.size());
    result.navigation_owners_by_id.reserve(
        document.entities.size() + document.prefab_instances.size());
    for (const SceneAssetRef& package : document.asset_packages) {
      std::string diagnostic;
      assets::AssetPackageStore& store = detail::sceneAssetPackageStore(assets);
      std::optional<assets::AssetPackageHandle> handle;
      if (!package.baked_cache_path.empty()) {
        handle = store.acquireBakedPackage(
            detail::resolveDocumentPath(document,
                                        package.baked_cache_path,
                                        desc.reference_root),
            detail::resolveDocumentPath(document,
                                        package.path,
                                        desc.reference_root),
            &diagnostic);
        if (!handle.has_value()) {
          spdlog::warn(
              "failed to restore baked scene asset package '{}' from '{}': {}; "
              "falling back to source package '{}'",
              package.id,
              package.baked_cache_path.generic_string(),
              diagnostic,
              package.path.generic_string());
          diagnostic.clear();
        }
      }
      if (!handle.has_value()) {
        handle = store.acquirePackage(
            detail::resolveDocumentPath(document,
                                        package.path,
                                        desc.reference_root),
            &diagnostic);
      }
      if (!handle.has_value()) {
        return fail_and_rollback("failed to import scene asset package '" +
                                 package.path.generic_string() + "': " +
                                 diagnostic);
      }
      try {
        result.asset_packages.push_back(*handle);
      } catch (...) {
        // The store reference exists even if copying its public handle into
        // the result fails. Release that unrecorded reference immediately.
        store.releasePackage(*handle);
        throw;
      }
    }

    if (desc.instantiate_authored_entities) {
      for (const SceneEntity& authored : document.entities) {
        const world::Entity entity = create_recorded_entity();
        result.entities_by_id[authored.id] = entity;
        result.navigation_owners_by_id["entity:" + authored.id] = entity;
        if (!authored.name.empty()) {
          world.setName(entity, authored.name);
        }
        world.add(entity, toTransform(authored.transform));
        scene.createNode(entity);
      }

      for (const SceneEntity& authored : document.entities) {
        if (authored.parent_id.empty()) {
          continue;
        }
        const auto child_it = result.entities_by_id.find(authored.id);
        const auto parent_it = result.entities_by_id.find(authored.parent_id);
        if (child_it == result.entities_by_id.end() ||
            parent_it == result.entities_by_id.end()) {
          return fail_and_rollback("missing authored entity hierarchy reference");
        }
        if (!scene.reparent(scene.ensureNode(child_it->second),
                            scene.ensureNode(parent_it->second))) {
          return fail_and_rollback("invalid authored entity hierarchy relationship");
        }
      }

      if (desc.attach_authored_components) {
        const prefabs::ComponentSerializationContext component_context{
            .resolve_entity_reference =
                [&](const nlohmann::json& reference)
                    -> std::optional<world::Entity> {
              const std::optional<std::string> id =
                  sceneEntityReferenceId(reference);
              if (!id.has_value()) return std::nullopt;
              const auto it = result.entities_by_id.find(*id);
              return it == result.entities_by_id.end()
                         ? std::nullopt
                         : std::optional<world::Entity>(it->second);
            },
        };
        for (const SceneEntity& authored : document.entities) {
          const auto entity_it = result.entities_by_id.find(authored.id);
          if (entity_it == result.entities_by_id.end() ||
              !detail::deserializeAuthoredComponents(world,
                                                     entity_it->second,
                                                     authored.components,
                                                     result,
                                                     component_context)) {
            return fail_and_rollback("failed to deserialize scene entity components");
          }
          resolveAuthoredComponentPaths(world,
                                        entity_it->second,
                                        document,
                                        desc.reference_root);
        }
      }
    }

    if (desc.instantiate_gltf_scenes) {
      for (const SceneAssetRef& scene_asset : document.gltf_scenes) {
        const assets::GltfSceneAsset* asset =
            detail::findGltfSceneAsset(assets,
                                       document,
                                       scene_asset,
                                       desc.reference_root);
        if (asset == nullptr) {
          return fail_and_rollback("missing registered glTF scene asset: " +
                                   scene_asset.id);
        }
        const world::GltfSceneImportResult imported =
            world::instantiateGltfSceneAsset(
                world,
                scene,
                assets,
                *asset,
                world::GltfSceneInstantiateOptions{
                    .create_synthetic_root = desc.create_synthetic_gltf_roots,
                    .autoplay_animations = desc.autoplay_gltf_animations,
                });
        if (!imported.valid()) {
          return fail_and_rollback("failed to instantiate glTF scene asset: " +
                                   scene_asset.id);
        }
        try {
          if (imported.entities.size() >
              result.entities.max_size() - result.entities.size()) {
            throw std::length_error(
                "scene glTF entity ownership exceeds vector capacity");
          }
          result.entities.reserve(result.entities.size() +
                                  imported.entities.size());
        } catch (...) {
          // No imported entity is owned by `result` yet. Tear down the whole
          // import before propagating the allocation failure.
          for (auto it = imported.entities.rbegin();
               it != imported.entities.rend();
               ++it) {
            const world::NodeId node = scene.findNode(*it);
            if (scene.isAlive(node)) scene.destroyNode(node);
            if (world.isAlive(*it)) world.destroyEntity(*it);
          }
          throw;
        }
        for (const world::Entity entity : imported.entities) {
          record_entity(entity);  // Capacity was reserved above.
        }
        // From here on, outer rollback owns the imported entities if a map
        // allocation or copy fails.
        result.gltf_scene_roots_by_id[scene_asset.id] = imported.root_entity;
        result.gltf_scene_entities_by_id[scene_asset.id] = imported.entities;
      }
    }

    if (desc.instantiate_prefabs) {
      for (const ScenePrefabInstance& prefab : document.prefab_instances) {
        prefabs::PrefabInstantiateDesc prefab_desc{};
        prefab_desc.root_transform = toTransform(prefab.transform);
        prefab_desc.assets = &assets;
        prefab_desc.variables = detail::prefabVariables(prefab.variables);
        std::optional<prefabs::PrefabInstance> instance =
            prefabs::instantiatePrefab(world,
                                       scene,
                                       detail::resolveDocumentPath(document,
                                                                   prefab.prefab_path,
                                                                   desc.reference_root),
                                       prefab_desc);
        if (!instance.has_value() || !instance->valid()) {
          return fail_and_rollback("failed to instantiate prefab: " + prefab.id);
        }
        // Capacity is reserved before instantiation, so this non-allocating
        // write transfers teardown responsibility to the scene result before
        // any string/map/handle copy can fail.
        result.prefab_roots.push_back(instance->root);
        result.prefab_roots_by_id[prefab.id] = instance->root;
        for (const auto& [node_id, entity] : instance->entities_by_id) {
          result.navigation_owners_by_id[
              "prefab:" + prefab.id + "/node:" + std::to_string(node_id)] =
              entity;
        }
        if (instance->asset_package.has_value()) {
          result.prefab_asset_packages.push_back(*instance->asset_package);
        }
        for (const world::Entity entity : instance->entities) {
          record_entity(entity);
        }
        if (!prefab.parent_entity_id.empty()) {
          const auto parent_it = result.entities_by_id.find(prefab.parent_entity_id);
          if (parent_it == result.entities_by_id.end()) {
            return fail_and_rollback("missing prefab parent entity: " +
                                     prefab.parent_entity_id);
          }
          if (!scene.reparent(scene.ensureNode(instance->root),
                              scene.ensureNode(parent_it->second))) {
            return fail_and_rollback("invalid prefab parent relationship: " +
                                     prefab.parent_entity_id);
          }
        }
        if (desc.attach_authored_components &&
            prefab.static_component.has_value()) {
          // Static membership is the narrow per-instance exception to linked
          // prefab component immutability. Apply it after source components
          // and parenting, before the scene-wide descendant materialization.
          world.add(instance->root, *prefab.static_component);
        }
      }
    }

    if (desc.attach_authored_components) {
      if (document.environment.has_value()) {
        const SceneEnvironment& environment = *document.environment;
        world::Entity entity{};
        if (!environment.entity_id.empty()) {
          const auto entity_it = result.entities_by_id.find(environment.entity_id);
          if (entity_it == result.entities_by_id.end()) {
            return fail_and_rollback("missing environment entity: " +
                                     environment.entity_id);
          }
          entity = entity_it->second;
        } else {
          entity = create_recorded_entity();
          if (!environment.id.empty()) {
            result.entities_by_id[environment.id] = entity;
          }
          world.add(entity, components::TransformComponent{});
          scene.createNode(entity);
        }
        world.add(entity, environment.component);
      }

      for (const SceneCamera& camera : document.cameras) {
        const auto entity_it = result.entities_by_id.find(camera.entity_id);
        if (entity_it == result.entities_by_id.end()) {
          return fail_and_rollback("missing camera entity: " + camera.entity_id);
        }
        components::CameraComponent runtime_camera = camera.component;
        runtime_camera.shader_override_vertex_path =
            detail::resolveDocumentPath(document,
                                        runtime_camera.shader_override_vertex_path,
                                        desc.reference_root);
        runtime_camera.shader_override_fragment_path =
            detail::resolveDocumentPath(document,
                                        runtime_camera.shader_override_fragment_path,
                                        desc.reference_root);
        world.add(entity_it->second, std::move(runtime_camera));
        result.cameras_by_id[camera.id] = entity_it->second;
      }

      for (const SceneLight& light : document.lights) {
        const auto entity_it = result.entities_by_id.find(light.entity_id);
        if (entity_it == result.entities_by_id.end()) {
          return fail_and_rollback("missing light entity: " + light.entity_id);
        }
        components::LightComponent runtime_light = light.component;
        runtime_light.mixed_bake_mask_bit = UINT32_MAX;
        world.add(entity_it->second, std::move(runtime_light));
        result.lights_by_id[light.id] = entity_it->second;
      }

      addStaticMeshComponents(world, document, result);
      materializeInheritedStaticComponents(world, scene);

      for (const SceneBakeDesc& bake : document.bakes) {
        applyRuntimeLightmapBindings(world,
                                     document,
                                     bake,
                                     desc.reference_root,
                                     assets,
                                     result);
      }

#if defined(KARMA_ENABLE_NAVIGATION)
      for (const SceneBakeDesc& bake : document.bakes) {
        applyRuntimeNavigationBindings(world,
                                       document,
                                       bake,
                                       desc.reference_root,
                                       result);
      }
#endif
    }
  } catch (const std::exception& e) {
    // Constructing a diagnostic can itself allocate, so teardown must happen
    // before formatting the exception text.
    rollback_resources();
    try {
      appendDiagnostic(result,
                       std::string("scene instantiation failed: ") + e.what());
    } catch (...) {
    }
    return result;
  } catch (...) {
    rollback_resources();
    try {
      appendDiagnostic(result,
                       "scene instantiation failed with an unknown exception");
    } catch (...) {
    }
    return result;
  }

  world::updateWorldTransforms(world, scene);
  result.success = true;
  return result;
}

SceneStaticBuildResult buildSceneStaticMetadata(
    const SceneDocument& document,
    const SceneInstantiateResult& instance,
    const world::World& world,
    const world::Scene& scene,
    const assets::AssetRegistry& assets,
    const SceneStaticBuildDesc& desc) {
  SceneStaticBuildResult result{};

  if (instance.world_instance_id == 0u ||
      instance.world_instance_id != world.instanceId() ||
      instance.scene_instance_id == 0u ||
      instance.scene_instance_id != scene.instanceId() ||
      instance.asset_registry != &assets) {
    result.success = false;
    result.diagnostics.push_back(
        "scene instance belongs to a different World, Scene graph, or asset registry");
    return result;
  }

  // First-class StaticComponent authoring takes precedence over the legacy
  // scene-level static record for the same runtime entity. Owner ids remain
  // stable across runs and also cover prefab nodes.
  std::vector<std::pair<std::string, world::Entity>> static_owners(
      instance.navigation_owners_by_id.begin(),
      instance.navigation_owners_by_id.end());
  std::sort(static_owners.begin(),
            static_owners.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.first < rhs.first;
            });
  std::unordered_set<uint64_t> first_class_entities;
  for (const auto& [owner_id, entity] : static_owners) {
    if (!world.isAlive(entity) ||
        !world.has<components::StaticComponent>(entity)) {
      continue;
    }
    bool legacy_only = false;
    constexpr std::string_view kEntityPrefix = "entity:";
    if (owner_id.starts_with(kEntityPrefix)) {
      const std::string_view authored_id(owner_id.data() + kEntityPrefix.size(),
                                         owner_id.size() - kEntityPrefix.size());
      const bool authored_first_class = std::any_of(
          document.entities.begin(),
          document.entities.end(),
          [&](const SceneEntity& authored) {
            return authored.id == authored_id &&
                   authored.components.is_object() &&
                   authored.components.contains("StaticComponent");
          });
      legacy_only = !authored_first_class && std::any_of(
          document.static_components.begin(),
          document.static_components.end(),
          [&](const SceneStaticComponent& legacy) {
            return legacy.entity_id == authored_id;
          });
    }
    if (legacy_only) {
      continue;
    }
    first_class_entities.insert(entityKey(entity));
    const auto& membership = world.get<components::StaticComponent>(entity);
    if (!membership.enabled || membership.flags == 0u) {
      ++result.skipped_static_components;
      continue;
    }
    if (!world.has<components::TransformComponent>(entity)) {
      result.success = false;
      result.diagnostics.push_back("static owner '" + owner_id +
                                   "' is missing a transform");
      ++result.skipped_static_components;
      continue;
    }
    if (desc.require_scene_node && !scene.isAlive(scene.findNode(entity))) {
      result.success = false;
      result.diagnostics.push_back("static owner '" + owner_id +
                                   "' is missing a scene node");
      ++result.skipped_static_components;
      continue;
    }

    const auto& transform = world.get<components::TransformComponent>(entity);
    const SceneTransform local_transform = localSceneTransform(transform);
    const SceneTransform world_transform = worldSceneTransform(transform);
    result.transforms.push_back(SceneStaticTransform{
        .static_component_id = owner_id,
        .entity_id = owner_id,
        .entity = entity,
        .local = local_transform,
        .world = world_transform,
    });

    const bool render_static =
        (membership.flags & components::StaticComponentRender) != 0u;
    if (!desc.build_mesh_bounds || !render_static ||
        !world.has<components::MeshComponent>(entity)) {
      continue;
    }
    const auto& mesh_component = world.get<components::MeshComponent>(entity);
    if (mesh_component.mesh_asset_key.empty()) {
      continue;
    }
    const world::MeshData* mesh =
        assets.findMeshAsset(mesh_component.mesh_asset_key);
    if (mesh == nullptr) {
      result.success = false;
      result.diagnostics.push_back("static owner '" + owner_id +
                                   "' references missing mesh asset '" +
                                   mesh_component.mesh_asset_key + "'");
      continue;
    }
    SceneStaticBounds bounds{
        .static_component_id = owner_id,
        .entity_id = owner_id,
        .mesh_asset_key = mesh_component.mesh_asset_key,
        .entity = entity,
    };
    if (computeMeshBounds(*mesh, world_transform, bounds)) {
      result.bounds.push_back(std::move(bounds));
    } else {
      result.success = false;
      result.diagnostics.push_back("static owner '" + owner_id +
                                   "' references mesh asset without valid vertices '" +
                                   mesh_component.mesh_asset_key + "'");
    }
  }

  for (const SceneStaticComponent& static_component : document.static_components) {
    if (!static_component.transform) {
      ++result.skipped_static_components;
      continue;
    }
    if (!desc.include_gltf_static_components && !static_component.gltf_scene_id.empty()) {
      ++result.skipped_static_components;
      continue;
    }

    const auto entity_it = instance.entities_by_id.find(static_component.entity_id);
    if (entity_it == instance.entities_by_id.end()) {
      ++result.skipped_static_components;
      continue;
    }

    const world::Entity entity = entity_it->second;
    if (first_class_entities.contains(entityKey(entity))) {
      continue;
    }
    if (!world.isAlive(entity) || !world.has<components::TransformComponent>(entity)) {
      result.success = false;
      result.diagnostics.push_back("static component '" + static_component.id +
                                   "' is missing an alive transform entity");
      ++result.skipped_static_components;
      continue;
    }

    if (desc.require_scene_node && !scene.isAlive(scene.findNode(entity))) {
      result.success = false;
      result.diagnostics.push_back("static component '" + static_component.id +
                                   "' is missing a scene node");
      ++result.skipped_static_components;
      continue;
    }

    const components::TransformComponent& transform =
        world.get<components::TransformComponent>(entity);
    const SceneTransform local_transform = localSceneTransform(transform);
    const SceneTransform world_transform = worldSceneTransform(transform);
    result.transforms.push_back(SceneStaticTransform{
        .static_component_id = static_component.id,
        .entity_id = static_component.entity_id,
        .entity = entity,
        .local = local_transform,
        .world = world_transform,
    });

    if (!desc.build_mesh_bounds || static_component.mesh_asset_key.empty()) {
      continue;
    }

    const world::MeshData* mesh = assets.findMeshAsset(static_component.mesh_asset_key);
    if (mesh == nullptr) {
      result.success = false;
      result.diagnostics.push_back("static component '" + static_component.id +
                                   "' references missing mesh asset '" +
                                   static_component.mesh_asset_key + "'");
      continue;
    }

    SceneStaticBounds bounds{
        .static_component_id = static_component.id,
        .entity_id = static_component.entity_id,
        .mesh_asset_key = static_component.mesh_asset_key,
        .entity = entity,
    };
    if (computeMeshBounds(*mesh, world_transform, bounds)) {
      result.bounds.push_back(bounds);
    } else {
      result.success = false;
      result.diagnostics.push_back("static component '" + static_component.id +
                                   "' references mesh asset without vertices '" +
                                   static_component.mesh_asset_key + "'");
    }
  }

  return result;
}

bool destroyScene(world::World& world,
                  world::Scene& scene,
                  SceneInstantiateResult& result) {
  if (result.world_instance_id == 0u ||
      result.world_instance_id != world.instanceId() ||
      result.scene_instance_id == 0u ||
      result.scene_instance_id != scene.instanceId()) {
    return false;
  }
  bool ok = true;

  for (auto it = result.prefab_roots.rbegin(); it != result.prefab_roots.rend(); ++it) {
    const bool root_was_alive = world.isAlive(*it);
    const bool destroyed = prefabs::destroyPrefab(world, scene, *it);
    ok = (destroyed || !root_was_alive) && ok;
  }

  for (auto it = result.entities.rbegin(); it != result.entities.rend(); ++it) {
    const world::Entity entity = *it;
    if (!world.isAlive(entity)) {
      continue;
    }
    const world::NodeId node = scene.findNode(entity);
    if (scene.isAlive(node)) {
      scene.destroyNode(node);
    }
    world.destroyEntity(entity);
  }

  const bool has_generated_assets =
      !result.generated_mesh_asset_keys.empty() ||
      !result.generated_texture_asset_keys.empty() ||
      !result.generated_material_asset_keys.empty();
  if (result.asset_registry != nullptr) {
    for (auto it = result.generated_material_asset_keys.rbegin();
         it != result.generated_material_asset_keys.rend(); ++it) {
      ok = result.asset_registry->unregisterMaterial(*it) && ok;
    }
    for (auto it = result.generated_texture_asset_keys.rbegin();
         it != result.generated_texture_asset_keys.rend(); ++it) {
      ok = result.asset_registry->unregisterTextureAsset(*it) && ok;
    }
    for (auto it = result.generated_mesh_asset_keys.rbegin();
         it != result.generated_mesh_asset_keys.rend(); ++it) {
      ok = result.asset_registry->unregisterMeshAsset(*it) && ok;
    }
  } else if (has_generated_assets) {
    ok = false;
  }

  if (result.asset_registry != nullptr && !result.asset_packages.empty()) {
    assets::AssetPackageStore& store = detail::sceneAssetPackageStore(*result.asset_registry);
    for (auto it = result.asset_packages.rbegin(); it != result.asset_packages.rend(); ++it) {
      ok = store.releasePackage(*it) && ok;
    }
  } else if (!result.asset_packages.empty()) {
    ok = false;
  }

  result.success = false;
  result.asset_packages.clear();
  result.prefab_asset_packages.clear();
  result.generated_mesh_asset_keys.clear();
  result.generated_texture_asset_keys.clear();
  result.generated_material_asset_keys.clear();
  result.entities.clear();
  result.prefab_roots.clear();
  result.entities_by_id.clear();
  result.gltf_scene_roots_by_id.clear();
  result.gltf_scene_entities_by_id.clear();
  result.prefab_roots_by_id.clear();
  result.cameras_by_id.clear();
  result.lights_by_id.clear();
  result.navigation_owners_by_id.clear();
  result.world_instance_id = 0u;
  result.scene_instance_id = 0u;
  result.asset_registry = nullptr;
  return ok;
}

}  // namespace karma::scenes
