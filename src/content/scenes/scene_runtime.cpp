#include "karma/scenes.h"

#include "scene_runtime_assets.h"
#include "scene_runtime_prefabs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/prefabs.h"

namespace karma::scenes {

namespace {

uint64_t entityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
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
  math::Vec3 local_max = local_min;
  for (const glm::vec3& vertex : mesh.vertices) {
    const math::Vec3 point = math::fromGlm(vertex);
    local_min = minVec(local_min, point);
    local_max = maxVec(local_max, point);
  }

  const std::array<math::Vec3, 8> corners = boundsCorners(local_min, local_max);
  math::Vec3 world_min = transformPoint(world_transform, corners.front());
  math::Vec3 world_max = world_min;
  for (const math::Vec3& corner : corners) {
    const math::Vec3 point = transformPoint(world_transform, corner);
    world_min = minVec(world_min, point);
    world_max = maxVec(world_max, point);
  }

  const math::Vec3 center = math::scale(math::add(world_min, world_max), 0.5f);
  float radius_squared = 0.0f;
  for (const math::Vec3& corner : corners) {
    const math::Vec3 point = transformPoint(world_transform, corner);
    radius_squared = std::max(radius_squared,
                              math::lengthSquared(math::subtract(point, center)));
  }

  out_bounds.local_min = local_min;
  out_bounds.local_max = local_max;
  out_bounds.world_min = world_min;
  out_bounds.world_max = world_max;
  out_bounds.world_center = center;
  out_bounds.world_radius = std::sqrt(radius_squared);
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
    if (!static_component.render || static_component.mesh_asset_key.empty()) {
      continue;
    }
    const auto entity_it = result.entities_by_id.find(static_component.entity_id);
    if (entity_it == result.entities_by_id.end() ||
        !world.isAlive(entity_it->second)) {
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

}  // namespace

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
  result.asset_registry = &assets;

  std::unordered_set<uint64_t> recorded_entities;
  auto record_entity = [&](world::Entity entity) {
    if (!entity.isValid()) {
      return;
    }
    if (recorded_entities.insert(entityKey(entity)).second) {
      result.entities.push_back(entity);
    }
  };

  auto fail_and_rollback = [&](std::string message) {
    appendDiagnostic(result, std::move(message));
    std::vector<std::string> diagnostics = result.diagnostics;
    destroyScene(world, scene, result);
    result.diagnostics = std::move(diagnostics);
    result.success = false;
    result.asset_registry = &assets;
    return result;
  };

  for (const SceneAssetRef& package : document.asset_packages) {
    std::string diagnostic;
    assets::AssetPackageStore& store = detail::sceneAssetPackageStore(assets);
    std::optional<assets::AssetPackageHandle> handle;
    if (!package.baked_cache_path.empty()) {
      handle = store.acquireBakedPackage(
          detail::resolveDocumentPath(document, package.baked_cache_path),
          &diagnostic);
      if (!handle.has_value()) {
        spdlog::warn("failed to restore baked scene asset package '{}' from '{}': {}; "
                     "falling back to source package '{}'",
                     package.id,
                     package.baked_cache_path.generic_string(),
                     diagnostic,
                     package.path.generic_string());
        diagnostic.clear();
      }
    }
    if (!handle.has_value()) {
      handle = store.acquirePackage(detail::resolveDocumentPath(document, package.path),
                                    &diagnostic);
    }
    if (!handle.has_value()) {
      return fail_and_rollback("failed to import scene asset package '" +
                               package.path.generic_string() + "': " + diagnostic);
    }
    result.asset_packages.push_back(*handle);
  }

  try {
    if (desc.instantiate_authored_entities) {
      for (const SceneEntity& authored : document.entities) {
        const world::Entity entity = world.createEntity();
        record_entity(entity);
        result.entities_by_id[authored.id] = entity;
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
        scene.reparent(scene.ensureNode(child_it->second),
                       scene.ensureNode(parent_it->second));
      }

      if (desc.attach_authored_components) {
        for (const SceneEntity& authored : document.entities) {
          const auto entity_it = result.entities_by_id.find(authored.id);
          if (entity_it == result.entities_by_id.end() ||
              !detail::deserializeAuthoredComponents(world,
                                                     entity_it->second,
                                                     authored.components,
                                                     result)) {
            return fail_and_rollback("failed to deserialize scene entity components");
          }
        }
      }
    }

    if (desc.instantiate_gltf_scenes) {
      for (const SceneAssetRef& scene_asset : document.gltf_scenes) {
        const assets::GltfSceneAsset* asset = detail::findGltfSceneAsset(assets, scene_asset);
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
        result.gltf_scene_roots_by_id[scene_asset.id] = imported.root_entity;
        result.gltf_scene_entities_by_id[scene_asset.id] = imported.entities;
        for (const world::Entity entity : imported.entities) {
          record_entity(entity);
        }
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
                                       detail::resolveDocumentPath(document, prefab.prefab_path),
                                       prefab_desc);
        if (!instance.has_value() || !instance->valid()) {
          return fail_and_rollback("failed to instantiate prefab: " + prefab.id);
        }
        result.prefab_roots.push_back(instance->root);
        result.prefab_roots_by_id[prefab.id] = instance->root;
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
          scene.reparent(scene.ensureNode(instance->root),
                         scene.ensureNode(parent_it->second));
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
          entity = world.createEntity();
          record_entity(entity);
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
        world.add(entity_it->second, camera.component);
        result.cameras_by_id[camera.id] = entity_it->second;
      }

      for (const SceneLight& light : document.lights) {
        const auto entity_it = result.entities_by_id.find(light.entity_id);
        if (entity_it == result.entities_by_id.end()) {
          return fail_and_rollback("missing light entity: " + light.entity_id);
        }
        world.add(entity_it->second, light.component);
        result.lights_by_id[light.id] = entity_it->second;
      }

      addStaticMeshComponents(world, document, result);
    }
  } catch (const std::exception& e) {
    return fail_and_rollback(std::string("scene instantiation failed: ") + e.what());
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
  bool ok = true;

  for (auto it = result.prefab_roots.rbegin(); it != result.prefab_roots.rend(); ++it) {
    if (world.isAlive(*it)) {
      ok = prefabs::destroyPrefab(world, scene, *it) && ok;
    }
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

  if (result.asset_registry != nullptr) {
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
  result.entities.clear();
  result.prefab_roots.clear();
  result.entities_by_id.clear();
  result.gltf_scene_roots_by_id.clear();
  result.gltf_scene_entities_by_id.clear();
  result.prefab_roots_by_id.clear();
  result.cameras_by_id.clear();
  result.lights_by_id.clear();
  result.asset_registry = nullptr;
  return ok;
}

}  // namespace karma::scenes
