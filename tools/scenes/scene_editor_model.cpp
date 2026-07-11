#include "scene_editor_model.h"

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/prefabs.h"
#include "karma/world.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <system_error>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace karma::tools::scene_editor {
namespace {

using Json = nlohmann::json;

bool fail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

bool isVersionOne(const Json& value) {
  if (value.is_number_unsigned()) {
    return value.get<uint64_t>() == 1u;
  }
  return value.is_number_integer() && value.get<int64_t>() == 1;
}

const char* viewportRenderModeName(ViewportRenderMode mode) {
  switch (mode) {
    case ViewportRenderMode::Rendered:
      return "rendered";
    case ViewportRenderMode::Diffuse:
      return "diffuse";
    case ViewportRenderMode::Texture:
      return "texture";
    case ViewportRenderMode::Wire:
      return "wire";
  }
  return "rendered";
}

std::optional<ViewportRenderMode> parseViewportRenderMode(
    std::string_view value) {
  if (value == "rendered") return ViewportRenderMode::Rendered;
  if (value == "diffuse") return ViewportRenderMode::Diffuse;
  if (value == "texture") return ViewportRenderMode::Texture;
  if (value == "wire") return ViewportRenderMode::Wire;
  return std::nullopt;
}

std::filesystem::path weakCanonical(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec) {
    return canonical;
  }
  canonical = std::filesystem::absolute(path, ec);
  return ec ? path.lexically_normal() : canonical.lexically_normal();
}

bool readJson(const std::filesystem::path& path, Json& out, std::string& diagnostic) {
  std::ifstream stream(path);
  if (!stream) {
    diagnostic = "failed to open " + path.string();
    return false;
  }
  try {
    stream >> out;
  } catch (const std::exception& e) {
    diagnostic = "failed to parse " + path.string() + ": " + e.what();
    return false;
  }
  return true;
}

bool atomicWriteJson(const std::filesystem::path& path,
                     const Json& json,
                     std::string* diagnostic) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return fail(diagnostic, "failed to create directory " +
                                  path.parent_path().string() + ": " + ec.message());
    }
  }
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return fail(diagnostic, "failed to open temporary file " + temporary.string());
    }
    stream << std::setw(2) << json << '\n';
    stream.flush();
    if (!stream) {
      std::filesystem::remove(temporary, ec);
      return fail(diagnostic, "failed to write temporary file " + temporary.string());
    }
  }
#if defined(_WIN32)
  std::filesystem::remove(path, ec);
  ec.clear();
#endif
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    std::filesystem::remove(temporary);
    return fail(diagnostic, "failed to replace " + path.string() + ": " + ec.message());
  }
  return true;
}

AssetKind kindFromPackageType(std::string_view type) {
  if (type == "mesh" || type == "mesh_source") return AssetKind::Mesh;
  if (type == "material" || type == "material_variant") return AssetKind::Material;
  if (type == "environment" || type == "environment_map") return AssetKind::Environment;
  if (type == "texture" || type == "texture_rgba8") return AssetKind::Texture;
  return AssetKind::Other;
}

std::string prefabDisplayName(const Json& root, const std::filesystem::path& path) {
  const auto nodes = root.find("nodes");
  const auto root_index = root.find("root");
  if (nodes != root.end() && nodes->is_array() && root_index != root.end() &&
      root_index->is_number_unsigned()) {
    const size_t index = root_index->get<size_t>();
    if (index < nodes->size() && (*nodes)[index].is_object()) {
      const auto name = (*nodes)[index].find("name");
      if (name != (*nodes)[index].end() && name->is_string() && !name->get_ref<const std::string&>().empty()) {
        return name->get<std::string>();
      }
    }
  }
  const std::string parent = path.parent_path().filename().string();
  return parent.empty() ? path.stem().string() : parent;
}

size_t estimateDocumentBytes(const scenes::SceneDocument& document) {
  size_t bytes = sizeof(document) + document.name.size();
  for (const auto& entity : document.entities) {
    bytes += sizeof(entity) + entity.id.size() + entity.name.size() +
             entity.parent_id.size() + entity.components.dump().size();
  }
  for (const auto& prefab : document.prefab_instances) {
    bytes += sizeof(prefab) + prefab.id.size() + prefab.prefab_path.string().size() +
             prefab.variables.dump().size();
  }
  bytes += document.asset_packages.size() * sizeof(scenes::SceneAssetRef);
  bytes += document.gltf_scenes.size() * sizeof(scenes::SceneAssetRef);
  bytes += document.lights.size() * sizeof(scenes::SceneLight);
  bytes += document.cameras.size() * sizeof(scenes::SceneCamera);
  return bytes;
}

std::string recoveryKey(const std::filesystem::path& scene_path) {
  const std::string input = weakCanonical(scene_path).generic_string();
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char byte : input) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= 1099511628211ull;
  }
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

std::filesystem::path legacyRecoveryPath(
    const std::filesystem::path& content_root,
    const std::filesystem::path& scene_path) {
  const std::string input = weakCanonical(scene_path).generic_string();
  const uint64_t hash = std::hash<std::string>{}(input);
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return content_root / ".karma" / "recovery" /
         (stream.str() + ".scene-recovery.json");
}

std::string componentDisplayName(std::string_view type_name) {
  constexpr std::string_view suffix = "Component";
  if (type_name.size() >= suffix.size() &&
      type_name.substr(type_name.size() - suffix.size()) == suffix) {
    type_name.remove_suffix(suffix.size());
  }
  std::string display;
  display.reserve(type_name.size() + 4u);
  for (size_t i = 0u; i < type_name.size(); ++i) {
    const unsigned char current = static_cast<unsigned char>(type_name[i]);
    const bool uppercase = std::isupper(current) != 0;
    const bool previous_lower =
        i > 0u && std::islower(static_cast<unsigned char>(type_name[i - 1u])) != 0;
    const bool acronym_boundary =
        i > 0u && i + 1u < type_name.size() && uppercase &&
        std::isupper(static_cast<unsigned char>(type_name[i - 1u])) != 0 &&
        std::islower(static_cast<unsigned char>(type_name[i + 1u])) != 0;
    if (uppercase && (previous_lower || acronym_boundary)) {
      display.push_back(' ');
    }
    display.push_back(type_name[i]);
  }
  return display.empty() ? std::string(type_name) : display;
}

Json defaultBoxColliderPayload() {
  return Json{
      {"type", "box"},
      {"is_trigger", false},
      {"debug_draw", false},
      {"shape",
       Json{{"center", Json::array({0.0f, 0.0f, 0.0f})},
            {"half_extents", Json::array({0.5f, 0.5f, 0.5f})}}},
  };
}

std::vector<std::string> componentDependencies(std::string_view type_name) {
  if (type_name == "InstancedMeshComponent") {
    return {"InstanceSetComponent"};
  }
  if (type_name == "RigidbodyComponent" ||
      type_name == "CharacterControllerComponent") {
    return {"ColliderComponent"};
  }
  if (type_name == "PhysicsVehicleComponent") {
    return {"RigidbodyComponent"};
  }
  if (type_name == "LightPulseComponent") {
    return {"LightComponent"};
  }
#if defined(KARMA_ENABLE_NAVIGATION)
  if (type_name == "NavTileCacheComponent" ||
      type_name == "NavCrowdComponent") {
    return {"NavMeshComponent"};
  }
#endif
  if (type_name == "NetworkAuthorityComponent" ||
      type_name == "NetworkReplicatedComponent") {
    return {"NetworkIdentityComponent"};
  }
  return {};
}

std::vector<std::string> componentOneOfDependencies(
    std::string_view type_name) {
  if (type_name == "LODComponent") {
    return {"MeshComponent", "InstancedMeshComponent", "FoliageComponent"};
  }
  return {};
}

bool stageComponentDependencies(world::World& world,
                                world::Entity entity,
                                const std::vector<std::string>& dependencies,
                                std::string* diagnostic) {
  for (const std::string& dependency : dependencies) {
    if (dependency == "TransformComponent") {
      continue;
    }
    if (dependency == "ColliderComponent") {
      if (!world.has<components::ColliderComponent>(entity)) {
        world.add(entity, components::ColliderComponent::box());
      }
      continue;
    }
    if (dependency == "LightComponent") {
      if (!world.has<components::LightComponent>(entity)) {
        world.add(entity, components::LightComponent{});
      }
      continue;
    }
    if (dependency == "RigidbodyComponent") {
      if (!world.has<components::ColliderComponent>(entity)) {
        world.add(entity, components::ColliderComponent::box());
      }
      if (!world.has<components::RigidbodyComponent>(entity)) {
        world.add(entity, components::RigidbodyComponent{});
      }
      continue;
    }
#if defined(KARMA_ENABLE_NAVIGATION)
    if (dependency == "NavMeshComponent") {
      if (!world.has<components::NavMeshComponent>(entity)) {
        world.add(entity, components::NavMeshComponent{});
      }
      continue;
    }
#endif
    if (dependency == "NetworkIdentityComponent") {
      if (!world.has<components::NetworkIdentityComponent>(entity)) {
        world.add(entity, components::NetworkIdentityComponent{});
      }
      continue;
    }
    const prefabs::ComponentSerializer* serializer =
        prefabs::componentSerializerRegistry().find(dependency);
    if (serializer != nullptr) {
      const Json candidate = dependency == "InstanceSetComponent"
                                 ? defaultInstanceSetComponentPayload()
                                 : Json::object();
      if (serializer->deserialize(world, entity, candidate) &&
          serializer->has(world, entity)) {
        continue;
      }
    }
    return fail(diagnostic,
                "component staging does not know how to create dependency: " +
                    dependency);
  }
  return true;
}

bool validateWithSerializer(const prefabs::ComponentSerializer& serializer,
                            const std::vector<std::string>& dependencies,
                            const Json& payload,
                            std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  try {
    world::World staging_world;
    const world::Entity entity = staging_world.createEntity();
    staging_world.add(entity, components::TransformComponent{});
    if (!stageComponentDependencies(
            staging_world, entity, dependencies, diagnostic)) {
      return false;
    }
    if (!serializer.deserialize(staging_world, entity, payload) ||
        !serializer.has(staging_world, entity)) {
      return fail(diagnostic,
                  "component payload failed validation for " +
                      serializer.type_name);
    }
    return true;
  } catch (const std::exception& error) {
    return fail(diagnostic,
                "component payload failed validation for " +
                    serializer.type_name + ": " + error.what());
  }
}

std::optional<Json> canonicalDefaultPayload(
    const prefabs::ComponentSerializer& serializer,
    const std::vector<std::string>& dependencies) {
  Json candidate = serializer.type_name == "ColliderComponent"
                       ? defaultBoxColliderPayload()
                       : (serializer.type_name == "LODComponent"
                              ? defaultLodComponentPayload()
                              : (serializer.type_name == "InstanceSetComponent"
                                     ? defaultInstanceSetComponentPayload()
                                     : Json::object()));
  try {
    world::World staging_world;
    const world::Entity entity = staging_world.createEntity();
    staging_world.add(entity, components::TransformComponent{});
    if (!stageComponentDependencies(staging_world, entity, dependencies, nullptr) ||
        !serializer.deserialize(staging_world, entity, candidate) ||
        !serializer.has(staging_world, entity)) {
      return std::nullopt;
    }
    return serializer.serialize(staging_world, entity);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

void configureComponentDescriptor(ComponentEditorDescriptor& descriptor) {
  const std::string& type = descriptor.type_name;
  descriptor.display_name = componentDisplayName(type);

  if (type == "TransformComponent") {
    descriptor.display_name = "Transform";
    descriptor.category = ComponentEditorCategory::General;
    descriptor.editor = ComponentEditorKind::Transform;
    descriptor.creation_policy = ComponentCreationPolicy::ContextualWorkflow;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::LivePatch;
    descriptor.removable = false;
  } else if (type == "StaticComponent") {
    descriptor.display_name = "Static";
    descriptor.category = ComponentEditorCategory::General;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::DocumentOnly;
  } else if (type == "AudioListenerComponent" ||
             type == "AudioSourceComponent") {
    descriptor.category = ComponentEditorCategory::Audio;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::DocumentOnly;
  } else if (type == "CameraComponent") {
    descriptor.category = ComponentEditorCategory::Rendering;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::LivePatch;
  } else if (type == "EnvironmentComponent") {
    descriptor.category = ComponentEditorCategory::Lighting;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::LivePatch;
  } else if (type == "MeshComponent") {
    descriptor.display_name = "Mesh Renderer";
    descriptor.category = ComponentEditorCategory::Rendering;
    descriptor.editor = ComponentEditorKind::Mesh;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
  } else if (type == "InstancedMeshComponent") {
    descriptor.display_name = "Instanced Mesh";
    descriptor.category = ComponentEditorCategory::Rendering;
    descriptor.editor = ComponentEditorKind::InstancedMesh;
  } else if (type == "InstanceSetComponent") {
    descriptor.display_name = "Instance Set";
    descriptor.category = ComponentEditorCategory::Rendering;
    descriptor.editor = ComponentEditorKind::InstanceSet;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::RebuildPreview;
  } else if (type == "LODComponent") {
    descriptor.display_name = "Level of Detail";
    descriptor.category = ComponentEditorCategory::Rendering;
    descriptor.editor = ComponentEditorKind::Lod;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::RebuildPreview;
  } else if (type == "FoliageComponent") {
    descriptor.category = ComponentEditorCategory::Terrain;
    descriptor.editor = ComponentEditorKind::Foliage;
    descriptor.creation_policy = ComponentCreationPolicy::ContextualWorkflow;
  } else if (type == "AnimatorComponent" ||
             type == "RootMotionComponent" ||
             type == "DeformableMeshComponent") {
    descriptor.category = ComponentEditorCategory::Animation;
  } else if (type == "LightComponent") {
    descriptor.category = ComponentEditorCategory::Lighting;
    descriptor.editor = ComponentEditorKind::Light;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::LivePatch;
  } else if (type == "LightPulseComponent") {
    descriptor.display_name = "Light Pulse";
    descriptor.category = ComponentEditorCategory::Lighting;
#if defined(KARMA_ENABLE_NAVIGATION)
  } else if (type == "NavMeshSurfaceComponent" ||
             type == "NavOffMeshLinkComponent" ||
             type == "NavConvexVolumeComponent" ||
             type == "NavMeshComponent" ||
             type == "NavMeshAgentComponent" ||
             type == "NavTileCacheComponent" ||
             type == "NavTileCacheObstacleComponent" ||
             type == "NavCrowdComponent" ||
             type == "NavCrowdAgentComponent") {
    descriptor.category = ComponentEditorCategory::Navigation;
#endif
  } else if (type == "NetworkIdentityComponent" ||
             type == "NetworkAuthorityComponent" ||
             type == "NetworkReplicatedComponent") {
    descriptor.category = ComponentEditorCategory::Networking;
  } else if (type == "ScriptComponent") {
    descriptor.category = ComponentEditorCategory::Scripting;
  } else if (type == "VisibilityComponent") {
    descriptor.category = ComponentEditorCategory::Rendering;
    descriptor.editor = ComponentEditorKind::Visibility;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::LivePatch;
  } else if (type == "RenderTagsComponent") {
    descriptor.display_name = "Render Tags";
    descriptor.category = ComponentEditorCategory::Rendering;
    descriptor.editor = ComponentEditorKind::RenderTags;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
  } else if (type == "TerrainComponent") {
    descriptor.category = ComponentEditorCategory::Terrain;
    descriptor.editor = ComponentEditorKind::Terrain;
    descriptor.creation_policy = ComponentCreationPolicy::ContextualWorkflow;
  } else if (type == "ColliderComponent") {
    descriptor.category = ComponentEditorCategory::Physics;
    descriptor.editor = ComponentEditorKind::Collider;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::DocumentOnly;
  } else if (type == "RigidbodyComponent") {
    descriptor.category = ComponentEditorCategory::Physics;
    descriptor.editor = ComponentEditorKind::Rigidbody;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::DocumentOnly;
  } else if (type == "PhysicsMaterialComponent") {
    descriptor.display_name = "Physics Material";
    descriptor.category = ComponentEditorCategory::Physics;
    descriptor.editor = ComponentEditorKind::PhysicsMaterial;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::DocumentOnly;
  } else if (type == "PhysicsCollisionFilterComponent") {
    descriptor.display_name = "Collision Filter";
    descriptor.category = ComponentEditorCategory::Physics;
    descriptor.editor = ComponentEditorKind::PhysicsCollisionFilter;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::DocumentOnly;
  } else if (type == "CharacterControllerComponent") {
    descriptor.display_name = "Character Controller";
    descriptor.category = ComponentEditorCategory::Physics;
    descriptor.editor = ComponentEditorKind::CharacterController;
    descriptor.creation_policy = ComponentCreationPolicy::DirectDefault;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::DocumentOnly;
  } else if (type == "CollisionListenerComponent" ||
             type == "ContactListenerComponent" ||
             type == "GroundContactComponent" ||
             type == "PhysicsConstraintComponent" ||
             type == "PhysicsSoftBodyComponent" ||
             type == "PhysicsVehicleComponent") {
    descriptor.category = ComponentEditorCategory::Physics;
    descriptor.runtime_update = ComponentRuntimeUpdatePolicy::DocumentOnly;
  } else if (type == "ParticleEffectComponent" ||
             type == "ParticleEffectOverrideComponent" ||
             type == "ParticleEmitterComponent" ||
             type == "ParticleBeamComponent" ||
             type == "VolumetricComponent") {
    descriptor.category = ComponentEditorCategory::Effects;
  } else if (type == "TagComponent") {
    descriptor.category = ComponentEditorCategory::General;
  }
}

bool colliderPayloadIsBox(const Json& payload) {
  const auto type = payload.find("type");
  return type != payload.end() && type->is_string() &&
         type->get_ref<const std::string&>() == "box";
}

bool hasCompatibleLodRenderSource(const Json& components) {
  if (!components.is_object()) return false;
  if (components.contains("MeshComponent") ||
      components.contains("InstancedMeshComponent")) {
    return true;
  }
  const auto foliage = components.find("FoliageComponent");
  if (foliage == components.end() || !foliage->is_object()) return false;
  const auto prefab_path = foliage->find("prefab_path");
  const auto mesh_asset_key = foliage->find("mesh_asset_key");
  return (prefab_path == foliage->end() ||
          (prefab_path->is_string() &&
           prefab_path->get_ref<const std::string&>().empty())) &&
         mesh_asset_key != foliage->end() && mesh_asset_key->is_string() &&
         !mesh_asset_key->get_ref<const std::string&>().empty();
}

}  // namespace

bool ComponentEditorRegistry::registerDescriptor(
    ComponentEditorDescriptor descriptor) {
  if (descriptor.type_name.empty() || descriptor.display_name.empty() ||
      !descriptor.default_payload || !descriptor.validate_payload) {
    return false;
  }
  const auto existing = indices_.find(descriptor.type_name);
  if (existing != indices_.end()) {
    descriptors_[existing->second] = std::move(descriptor);
    return true;
  }
  indices_[descriptor.type_name] = descriptors_.size();
  descriptors_.push_back(std::move(descriptor));
  return true;
}

const ComponentEditorDescriptor* ComponentEditorRegistry::find(
    std::string_view type_name) const {
  const auto descriptor = indices_.find(std::string(type_name));
  return descriptor == indices_.end() ? nullptr
                                      : &descriptors_[descriptor->second];
}

Json defaultLodComponentPayload() {
  return Json{{"levels", Json::array()}};
}

Json defaultInstanceSetComponentPayload() {
  return Json{
      {"gpu_layout", "matrix4x4_params"},
      {"instances", Json::array()},
      {"planar_instances", Json::array()},
      {"instance_revision", 0u},
      {"dynamic", false},
  };
}

bool validateLodComponentPayload(const Json& payload,
                                 std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!payload.is_object()) {
    return fail(diagnostic, "LODComponent payload must be an object");
  }
  const auto levels = payload.find("levels");
  if (levels == payload.end() || !levels->is_array()) {
    return fail(diagnostic, "LODComponent requires an array field 'levels'");
  }
  if (levels->size() > kMaxEditorLodLevels) {
    return fail(diagnostic, "LODComponent supports at most three levels");
  }
  float previous_distance = 0.0f;
  for (size_t index = 0u; index < levels->size(); ++index) {
    const Json& level = (*levels)[index];
    if (!level.is_object()) {
      return fail(diagnostic, "LOD level " + std::to_string(index) +
                                  " must be an object");
    }
    const auto distance = level.find("start_distance");
    const auto mesh = level.find("mesh_asset_key");
    const auto materials = level.find("materials");
    const auto mode = level.find("render_mode");
    const auto shadows = level.find("shadow_visible");
    if (distance == level.end() || !distance->is_number() ||
        mesh == level.end() || !mesh->is_string() ||
        materials == level.end() || !materials->is_array() ||
        mode == level.end() || !mode->is_string() ||
        shadows == level.end() || !shadows->is_boolean()) {
      return fail(diagnostic, "LOD level " + std::to_string(index) +
                                  " does not match the level schema");
    }
    const float start_distance = distance->get<float>();
    if (!std::isfinite(start_distance) || start_distance <= 0.0f ||
        (index != 0u && start_distance <= previous_distance)) {
      return fail(diagnostic,
                  "LOD distances must be finite, positive, and strictly increasing");
    }
    previous_distance = start_distance;
    if (mesh->get_ref<const std::string&>().empty()) {
      return fail(diagnostic, "LOD level mesh_asset_key cannot be empty");
    }
    const std::string& render_mode = mode->get_ref<const std::string&>();
    if (render_mode != "mesh" && render_mode != "upright_billboard") {
      return fail(diagnostic,
                  "LOD render_mode must be 'mesh' or 'upright_billboard'");
    }
    std::unordered_set<uint32_t> slots;
    for (const Json& material : *materials) {
      if (!material.is_object()) {
        return fail(diagnostic, "LOD material entries must be objects");
      }
      const auto slot = material.find("slot");
      const auto key = material.find("material_key");
      if (slot == material.end() ||
          (!slot->is_number_unsigned() && !slot->is_number_integer()) ||
          key == material.end() || !key->is_string() ||
          key->get_ref<const std::string&>().empty()) {
        return fail(diagnostic,
                    "LOD materials require an unsigned slot and material_key");
      }
      const int64_t signed_slot = slot->is_number_unsigned()
                                      ? static_cast<int64_t>(slot->get<uint64_t>())
                                      : slot->get<int64_t>();
      if (signed_slot < 0 ||
          static_cast<uint64_t>(signed_slot) > UINT32_MAX ||
          !slots.insert(static_cast<uint32_t>(signed_slot)).second) {
        return fail(diagnostic,
                    "LOD material slots must be unique unsigned 32-bit values");
      }
    }
  }
  return true;
}

bool validateInstanceSetComponentPayload(const Json& payload,
                                         std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!payload.is_object()) {
    return fail(diagnostic, "InstanceSetComponent payload must be an object");
  }
  const auto layout = payload.find("gpu_layout");
  const auto matrix_instances = payload.find("instances");
  const auto planar_instances = payload.find("planar_instances");
  const auto revision = payload.find("instance_revision");
  const auto dynamic = payload.find("dynamic");
  if (layout == payload.end() || !layout->is_string() ||
      matrix_instances == payload.end() || !matrix_instances->is_array() ||
      planar_instances == payload.end() || !planar_instances->is_array() ||
      revision == payload.end() ||
      (!revision->is_number_unsigned() && !revision->is_number_integer()) ||
      dynamic == payload.end() || !dynamic->is_boolean()) {
    return fail(diagnostic,
                "InstanceSetComponent does not match the expected schema");
  }
  const std::string& layout_name = layout->get_ref<const std::string&>();
  if (layout_name != "matrix4x4_params" &&
      layout_name != "position_yaw_scale_params") {
    return fail(diagnostic, "InstanceSetComponent gpu_layout is unsupported");
  }
  if (revision->is_number_integer() && revision->get<int64_t>() < 0) {
    return fail(diagnostic,
                "InstanceSetComponent instance_revision cannot be negative");
  }
  const auto finite_array = [](const Json& value, size_t size) {
    if (!value.is_array() || value.size() != size) return false;
    return std::all_of(value.begin(), value.end(), [](const Json& scalar) {
      return scalar.is_number() && std::isfinite(scalar.get<double>());
    });
  };
  for (const Json& instance : *matrix_instances) {
    if (!instance.is_object() ||
        !finite_array(instance.value("position", Json{}), 3u) ||
        !finite_array(instance.value("rotation", Json{}), 4u) ||
        !finite_array(instance.value("scale", Json{}), 3u) ||
        !finite_array(instance.value("params", Json{}), 4u)) {
      return fail(diagnostic,
                  "InstanceSetComponent matrix instances are malformed");
    }
  }
  for (const Json& instance : *planar_instances) {
    const auto yaw = instance.is_object()
                         ? instance.find("yaw_radians")
                         : instance.end();
    if (!instance.is_object() ||
        !finite_array(instance.value("position", Json{}), 3u) ||
        yaw == instance.end() || !yaw->is_number() ||
        !std::isfinite(yaw->get<double>()) ||
        !finite_array(instance.value("scale", Json{}), 3u) ||
        !finite_array(instance.value("params", Json{}), 4u)) {
      return fail(diagnostic,
                  "InstanceSetComponent planar instances are malformed");
    }
  }
  if (layout_name == "matrix4x4_params" && !planar_instances->empty()) {
    return fail(diagnostic,
                "matrix4x4_params cannot contain planar_instances");
  }
  if (layout_name == "position_yaw_scale_params" &&
      !matrix_instances->empty()) {
    return fail(diagnostic,
                "position_yaw_scale_params cannot contain matrix instances");
  }
  return true;
}

ComponentEditorRegistry buildComponentEditorRegistry() {
  prefabs::ensureBuiltinComponentSerializers();
  ComponentEditorRegistry registry;
  for (const prefabs::ComponentSerializer& serializer :
       prefabs::componentSerializerRegistry().serializers()) {
    ComponentEditorDescriptor descriptor{};
    descriptor.type_name = serializer.type_name;
    descriptor.dependencies = componentDependencies(serializer.type_name);
    descriptor.one_of_dependencies =
        componentOneOfDependencies(serializer.type_name);
    configureComponentDescriptor(descriptor);

    const Json default_payload =
        canonicalDefaultPayload(serializer, descriptor.dependencies)
            .value_or(Json::object());
    descriptor.default_payload =
        [default_payload]() { return default_payload; };
    descriptor.validate_payload =
        [serializer, dependencies = descriptor.dependencies](
            const Json& payload, std::string* diagnostic) {
          return validateWithSerializer(
              serializer, dependencies, payload, diagnostic);
        };
    registry.registerDescriptor(std::move(descriptor));
  }

  // Keep tooling source-compatible while the split render-component
  // serializers are being rolled out across build profiles. When the engine
  // serializers are present, the descriptors above remain authoritative and
  // validate through a staging World.
  if (registry.find("LODComponent") == nullptr) {
    ComponentEditorDescriptor descriptor{};
    descriptor.type_name = "LODComponent";
    descriptor.dependencies = componentDependencies(descriptor.type_name);
    descriptor.one_of_dependencies =
        componentOneOfDependencies(descriptor.type_name);
    configureComponentDescriptor(descriptor);
    descriptor.default_payload = [] { return defaultLodComponentPayload(); };
    descriptor.validate_payload = validateLodComponentPayload;
    registry.registerDescriptor(std::move(descriptor));
  }
  if (registry.find("InstanceSetComponent") == nullptr) {
    ComponentEditorDescriptor descriptor{};
    descriptor.type_name = "InstanceSetComponent";
    descriptor.dependencies = componentDependencies(descriptor.type_name);
    configureComponentDescriptor(descriptor);
    descriptor.default_payload = [] {
      return defaultInstanceSetComponentPayload();
    };
    descriptor.validate_payload = validateInstanceSetComponentPayload;
    registry.registerDescriptor(std::move(descriptor));
  }
  return registry;
}

bool validateComponentPayload(const ComponentEditorRegistry& registry,
                              std::string_view type_name,
                              const Json& payload,
                              std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  const ComponentEditorDescriptor* descriptor = registry.find(type_name);
  if (descriptor == nullptr) {
    return fail(diagnostic,
                "component type is not registered: " + std::string(type_name));
  }
  return descriptor->validate_payload(payload, diagnostic);
}

bool addComponentWithDependencies(
    scenes::SceneEntity& entity,
    const ComponentEditorRegistry& registry,
    std::string_view type_name,
    const Json& payload,
    std::vector<std::string>* added_types,
    std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (added_types != nullptr) added_types->clear();
  if (!entity.components.is_object()) {
    return fail(diagnostic, "scene entity components must be an object");
  }
  const ComponentEditorDescriptor* target = registry.find(type_name);
  if (target == nullptr) {
    return fail(diagnostic,
                "component type is not registered: " + std::string(type_name));
  }
  if (target->creation_policy == ComponentCreationPolicy::ContextualWorkflow) {
    return fail(diagnostic,
                target->display_name +
                    " must be created through its contextual authoring workflow");
  }
  if (type_name == "TransformComponent" ||
      entity.components.contains(std::string(type_name))) {
    return fail(diagnostic,
                "scene entity already has component: " +
                    std::string(type_name));
  }

  Json staged_components = entity.components;
  if (!target->one_of_dependencies.empty() &&
      std::none_of(target->one_of_dependencies.begin(),
                   target->one_of_dependencies.end(),
                   [&](const std::string& dependency) {
                     return staged_components.contains(dependency);
                   })) {
    std::string choices;
    for (const std::string& dependency : target->one_of_dependencies) {
      if (!choices.empty()) choices += ", ";
      choices += dependency;
    }
    return fail(diagnostic,
                target->display_name + " requires one compatible source: " +
                    choices);
  }
  if (type_name == "LODComponent" &&
      !hasCompatibleLodRenderSource(staged_components)) {
    return fail(diagnostic,
                "Level of Detail requires a Mesh, Instanced Mesh, or "
                "direct-mesh Foliage source");
  }

  std::vector<std::string> staged_added;
  for (const std::string& dependency : target->dependencies) {
    if (dependency == "TransformComponent" ||
        staged_components.contains(dependency)) {
      continue;
    }
    const ComponentEditorDescriptor* dependency_descriptor =
        registry.find(dependency);
    if (dependency_descriptor == nullptr ||
        dependency_descriptor->creation_policy !=
            ComponentCreationPolicy::DirectDefault) {
      return fail(diagnostic,
                  "component dependency cannot be created automatically: " +
                      dependency);
    }
    const Json dependency_payload = dependency_descriptor->default_payload();
    if (!dependency_descriptor->validate_payload(dependency_payload, diagnostic)) {
      return false;
    }
    staged_components[dependency] = dependency_payload;
    staged_added.push_back(dependency);
  }

  if (type_name == "CharacterControllerComponent") {
    const auto collider = staged_components.find("ColliderComponent");
    if (collider == staged_components.end() || !colliderPayloadIsBox(*collider)) {
      return fail(diagnostic,
                  "Character Controller requires a box Collider component");
    }
  }
  if (!target->validate_payload(payload, diagnostic)) {
    return false;
  }
  staged_components[std::string(type_name)] = payload;
  staged_added.push_back(std::string(type_name));
  entity.components = std::move(staged_components);
  if (added_types != nullptr) {
    *added_types = std::move(staged_added);
  }
  return true;
}

bool addDefaultComponentWithDependencies(
    scenes::SceneEntity& entity,
    const ComponentEditorRegistry& registry,
    std::string_view type_name,
    std::vector<std::string>* added_types,
    std::string* diagnostic) {
  const ComponentEditorDescriptor* descriptor = registry.find(type_name);
  if (descriptor == nullptr) {
    return fail(diagnostic,
                "component type is not registered: " + std::string(type_name));
  }
  if (descriptor->creation_policy ==
      ComponentCreationPolicy::ValidatedJsonDraft) {
    return fail(diagnostic,
                descriptor->display_name +
                    " requires a validated JSON draft before it can be added");
  }
  return addComponentWithDependencies(entity,
                                      registry,
                                      type_name,
                                      descriptor->default_payload(),
                                      added_types,
                                      diagnostic);
}

bool replaceComponentPayload(scenes::SceneEntity& entity,
                             const ComponentEditorRegistry& registry,
                             std::string_view type_name,
                             const Json& payload,
                             std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!entity.components.is_object() ||
      !entity.components.contains(std::string(type_name))) {
    return fail(diagnostic,
                "scene entity does not have component: " +
                    std::string(type_name));
  }
  if (type_name == "ColliderComponent" &&
      entity.components.contains("CharacterControllerComponent") &&
      !colliderPayloadIsBox(payload)) {
    return fail(diagnostic,
                "Character Controller requires a box Collider component");
  }
  if (!validateComponentPayload(registry, type_name, payload, diagnostic)) {
    return false;
  }
  Json staged_components = entity.components;
  staged_components[std::string(type_name)] = payload;
  if (staged_components.contains("LODComponent") &&
      !hasCompatibleLodRenderSource(staged_components)) {
    return fail(diagnostic,
                "Level of Detail requires a Mesh, Instanced Mesh, or "
                "direct-mesh Foliage source");
  }
  entity.components = std::move(staged_components);
  return true;
}

std::vector<std::string> componentRemovalBlockers(
    const scenes::SceneEntity& entity,
    const ComponentEditorRegistry& registry,
    std::string_view type_name) {
  std::vector<std::string> blockers;
  if (!entity.components.is_object()) {
    return blockers;
  }
  for (auto component = entity.components.begin();
       component != entity.components.end(); ++component) {
    const ComponentEditorDescriptor* descriptor = registry.find(component.key());
    if (descriptor != nullptr &&
        std::find(descriptor->dependencies.begin(),
                  descriptor->dependencies.end(),
                  type_name) != descriptor->dependencies.end()) {
      blockers.push_back(component.key());
      continue;
    }
    if (descriptor != nullptr &&
        std::find(descriptor->one_of_dependencies.begin(),
                  descriptor->one_of_dependencies.end(),
                  type_name) != descriptor->one_of_dependencies.end()) {
      if (component.key() == "LODComponent") {
        Json remaining = entity.components;
        remaining.erase(std::string(type_name));
        if (!hasCompatibleLodRenderSource(remaining)) {
          blockers.push_back(component.key());
        }
        continue;
      }
      const bool has_alternative = std::any_of(
          descriptor->one_of_dependencies.begin(),
          descriptor->one_of_dependencies.end(),
          [&](const std::string& dependency) {
            return dependency != type_name &&
                   entity.components.contains(dependency);
          });
      if (!has_alternative) blockers.push_back(component.key());
    }
  }
  std::sort(blockers.begin(), blockers.end());
  return blockers;
}

bool removeComponentsTogether(scenes::SceneEntity& entity,
                              const ComponentEditorRegistry& registry,
                              const std::vector<std::string>& type_names,
                              std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!entity.components.is_object()) {
    return fail(diagnostic, "scene entity components must be an object");
  }
  if (type_names.empty()) {
    return fail(diagnostic, "component removal request is empty");
  }
  std::unordered_set<std::string> removal_set;
  for (const std::string& type_name : type_names) {
    const ComponentEditorDescriptor* descriptor = registry.find(type_name);
    if (descriptor == nullptr) {
      return fail(diagnostic,
                  "component type is not registered: " + type_name);
    }
    if (!descriptor->removable) {
      return fail(diagnostic,
                  "component cannot be removed: " + type_name);
    }
    if (!entity.components.contains(type_name)) {
      return fail(diagnostic,
                  "scene entity does not have component: " + type_name);
    }
    if (!removal_set.insert(type_name).second) {
      return fail(diagnostic,
                  "component removal request contains a duplicate: " +
                      type_name);
    }
  }

  for (const std::string& type_name : removal_set) {
    for (const std::string& blocker :
         componentRemovalBlockers(entity, registry, type_name)) {
      if (!removal_set.contains(blocker)) {
        return fail(diagnostic,
                    "cannot remove " + type_name + " while " + blocker +
                        " depends on it");
      }
    }
  }

  for (auto component = entity.components.begin();
       component != entity.components.end(); ++component) {
    if (removal_set.contains(component.key())) continue;
    const ComponentEditorDescriptor* descriptor = registry.find(component.key());
    if (descriptor == nullptr || descriptor->one_of_dependencies.empty()) {
      continue;
    }
    const bool had_compatible_source = std::any_of(
        descriptor->one_of_dependencies.begin(),
        descriptor->one_of_dependencies.end(),
        [&](const std::string& dependency) {
          return entity.components.contains(dependency);
        });
    const bool keeps_compatible_source = std::any_of(
        descriptor->one_of_dependencies.begin(),
        descriptor->one_of_dependencies.end(),
        [&](const std::string& dependency) {
          return entity.components.contains(dependency) &&
                 !removal_set.contains(dependency);
        });
    if (had_compatible_source && !keeps_compatible_source) {
      return fail(diagnostic,
                  "cannot remove all compatible render sources while " +
                      component.key() + " depends on one");
    }
  }

  Json staged_components = entity.components;
  for (const std::string& type_name : removal_set) {
    staged_components.erase(type_name);
  }
  if (staged_components.contains("LODComponent") &&
      !hasCompatibleLodRenderSource(staged_components)) {
    return fail(diagnostic,
                "cannot remove all compatible render sources while "
                "LODComponent depends on one");
  }
  entity.components = std::move(staged_components);
  return true;
}

namespace {

Json prefabDocumentJson(const prefabs::PrefabDocument& document) {
  Json nodes = Json::array();
  for (const prefabs::PrefabNode& node : document.nodes) {
    nodes.push_back(Json{
        {"id", node.id},
        {"name", node.name},
        {"parent", node.parent.has_value() ? Json(*node.parent) : Json(nullptr)},
        {"components", node.components},
    });
  }
  Json out{{"version", document.version},
           {"root", document.root},
           {"nodes", std::move(nodes)}};
  if (document.variables.is_object() && !document.variables.empty()) {
    out["variables"] = document.variables;
  }
  return out;
}

std::string prefabSourceHash(const std::filesystem::path& path) {
  return assets::hashFile(path).value_or(std::string{});
}

bool editablePrefabComponent(std::string_view type_name) {
  return type_name == "MeshComponent" || type_name == "LODComponent";
}

bool mergePrefabDraftIntoSource(const Json& source,
                                const prefabs::PrefabDocument& document,
                                Json& out,
                                std::string* diagnostic) {
  if (!source.is_object()) {
    return fail(diagnostic, "prefab source JSON is not an object");
  }
  const auto source_nodes = source.find("nodes");
  if (source_nodes == source.end() || !source_nodes->is_array() ||
      source_nodes->size() != document.nodes.size()) {
    return fail(diagnostic,
                "prefab source node structure changed outside the draft");
  }
  out = source;
  for (size_t index = 0u; index < document.nodes.size(); ++index) {
    Json& raw_node = out["nodes"][index];
    if (!raw_node.is_object() ||
        raw_node.value("id", UINT32_MAX) != document.nodes[index].id) {
      return fail(diagnostic,
                  "prefab source node identities changed outside the draft");
    }
    raw_node["components"] = document.nodes[index].components;
  }
  return true;
}

std::string joinPrefabDiagnostics(const prefabs::PrefabLoadResult& loaded) {
  std::string message;
  for (const std::string& entry : loaded.diagnostics) {
    if (!message.empty()) message += '\n';
    message += entry;
  }
  return message.empty() ? std::string("prefab validation failed") : message;
}

}  // namespace

bool PrefabAssetDraft::dirty() const {
  return prefabDocumentJson(document_) != prefabDocumentJson(saved_document_);
}

std::string_view PrefabAssetDraft::undoLabel() const {
  return canUndo() ? std::string_view(history_[cursor_ - 1u].label)
                   : std::string_view{};
}

std::string_view PrefabAssetDraft::redoLabel() const {
  return canRedo() ? std::string_view(history_[cursor_].label)
                   : std::string_view{};
}

void PrefabAssetDraft::push(std::string label,
                            prefabs::PrefabDocument before,
                            prefabs::PrefabDocument after) {
  if (prefabDocumentJson(before) == prefabDocumentJson(after)) return;
  if (cursor_ < history_.size()) {
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                   history_.end());
  }
  history_.push_back(Entry{
      .label = std::move(label),
      .before = std::move(before),
      .after = std::move(after),
  });
  cursor_ = history_.size();
}

bool PrefabAssetDraft::setNodeComponent(
    size_t node_index,
    std::string_view type_name,
    const Json& payload,
    const ComponentEditorRegistry& registry,
    std::string label,
    std::string* diagnostic,
    bool coalesce) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!editablePrefabComponent(type_name)) {
    return fail(diagnostic,
                "the focused prefab editor only supports MeshComponent and "
                "LODComponent");
  }
  if (node_index >= document_.nodes.size()) {
    return fail(diagnostic, "prefab draft node index is out of range");
  }
  if (!coalesce ||
      (coalesced_before_.has_value() &&
       (coalesced_node_index_ != node_index ||
        coalesced_type_name_ != type_name || coalesced_label_ != label))) {
    finishCoalescedEdit();
  }
  prefabs::PrefabDocument before = document_;
  scenes::SceneEntity staging{};
  staging.components = document_.nodes[node_index].components;
  const std::string type(type_name);
  const bool present = staging.components.contains(type);
  const bool changed = present
                           ? replaceComponentPayload(staging,
                                                     registry,
                                                     type_name,
                                                     payload,
                                                     diagnostic)
                           : addComponentWithDependencies(staging,
                                                          registry,
                                                          type_name,
                                                          payload,
                                                          nullptr,
                                                          diagnostic);
  if (!changed) return false;
  document_.nodes[node_index].components = std::move(staging.components);
  if (coalesce) {
    if (!coalesced_before_.has_value()) {
      coalesced_before_ = std::move(before);
      coalesced_label_ = std::move(label);
      coalesced_node_index_ = node_index;
      coalesced_type_name_ = type;
    }
  } else {
    push(std::move(label), std::move(before), document_);
  }
  return true;
}

bool PrefabAssetDraft::removeNodeComponent(
    size_t node_index,
    std::string_view type_name,
    const ComponentEditorRegistry& registry,
    std::string label,
    std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!editablePrefabComponent(type_name)) {
    return fail(diagnostic,
                "the focused prefab editor only supports MeshComponent and "
                "LODComponent");
  }
  if (node_index >= document_.nodes.size()) {
    return fail(diagnostic, "prefab draft node index is out of range");
  }
  finishCoalescedEdit();
  prefabs::PrefabDocument before = document_;
  scenes::SceneEntity staging{};
  staging.components = document_.nodes[node_index].components;
  if (!removeComponentsTogether(
          staging, registry, {std::string(type_name)}, diagnostic)) {
    return false;
  }
  document_.nodes[node_index].components = std::move(staging.components);
  push(std::move(label), std::move(before), document_);
  return true;
}

bool PrefabAssetDraft::undo() {
  finishCoalescedEdit();
  if (!canUndo()) return false;
  document_ = history_[cursor_ - 1u].before;
  --cursor_;
  return true;
}

bool PrefabAssetDraft::redo() {
  finishCoalescedEdit();
  if (!canRedo()) return false;
  document_ = history_[cursor_].after;
  ++cursor_;
  return true;
}

void PrefabAssetDraft::finishCoalescedEdit() {
  if (!coalesced_before_.has_value()) return;
  push(std::move(coalesced_label_),
       std::move(*coalesced_before_),
       document_);
  coalesced_before_.reset();
  coalesced_label_.clear();
  coalesced_type_name_.clear();
  coalesced_node_index_ = 0u;
}

bool PrefabAssetDraft::sourceChangedExternally() const {
  if (!valid()) return false;
  const std::string current = prefabSourceHash(source_path_);
  return current.empty() || current != source_hash_;
}

bool PrefabAssetDraft::save(const ComponentEditorRegistry& registry,
                            std::string* diagnostic) {
  (void)registry;
  finishCoalescedEdit();
  if (diagnostic != nullptr) diagnostic->clear();
  if (!valid()) return fail(diagnostic, "prefab draft is not open");
  if (sourceChangedExternally()) {
    return fail(diagnostic,
                "prefab source changed externally; Revert before saving");
  }
  Json serialized;
  if (!mergePrefabDraftIntoSource(
          source_json_, document_, serialized, diagnostic)) {
    return false;
  }
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path validation_path =
      source_path_.string() + ".scene-editor-validate-" +
      std::to_string(stamp) + ".json";
  if (!atomicWriteJson(validation_path, serialized, diagnostic)) return false;
  const prefabs::PrefabLoadResult validated =
      prefabs::loadPrefabDocument(validation_path);
  std::error_code ignored;
  std::filesystem::remove(validation_path, ignored);
  if (!validated.success()) {
    return fail(diagnostic, joinPrefabDiagnostics(validated));
  }
  if (sourceChangedExternally()) {
    return fail(diagnostic,
                "prefab source changed externally while validating; Save was cancelled");
  }
  if (!atomicWriteJson(source_path_, serialized, diagnostic)) return false;
  source_json_ = std::move(serialized);
  source_hash_ = prefabSourceHash(source_path_);
  if (source_hash_.empty()) {
    return fail(diagnostic,
                "prefab saved but its source fingerprint could not be refreshed");
  }
  saved_document_ = document_;
  return true;
}

bool PrefabAssetDraft::revert(std::string* diagnostic) {
  std::optional<PrefabAssetDraft> replacement =
      openPrefabAssetDraft(source_path_, diagnostic);
  if (!replacement.has_value()) return false;
  *this = std::move(*replacement);
  return true;
}

std::optional<PrefabAssetDraft> openPrefabAssetDraft(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  const prefabs::PrefabLoadResult loaded = prefabs::loadPrefabDocument(path);
  if (!loaded.success() || !loaded.document.has_value()) {
    fail(diagnostic, joinPrefabDiagnostics(loaded));
    return std::nullopt;
  }
  Json source;
  std::string read_error;
  if (!readJson(loaded.source_path, source, read_error)) {
    fail(diagnostic, std::move(read_error));
    return std::nullopt;
  }
  const std::string hash = prefabSourceHash(loaded.source_path);
  if (hash.empty()) {
    fail(diagnostic, "failed to fingerprint prefab source " +
                         loaded.source_path.string());
    return std::nullopt;
  }
  PrefabAssetDraft draft{};
  draft.source_path_ = loaded.source_path;
  draft.source_json_ = std::move(source);
  draft.saved_document_ = *loaded.document;
  draft.document_ = *loaded.document;
  draft.source_hash_ = hash;
  return draft;
}

void applyEditorCameraLookDelta(EditorOrbitCamera& camera,
                                float mouse_delta_x,
                                float mouse_delta_y,
                                float sensitivity) {
  camera.yaw -= mouse_delta_x * sensitivity;
  camera.pitch = std::clamp(camera.pitch - mouse_delta_y * sensitivity,
                            -1.55f,
                            1.55f);
}

scenes::SceneTransform editorOrbitCameraTransform(
    const EditorOrbitCamera& camera) {
  const math::Quat rotation = math::fromYawPitch(camera.yaw, camera.pitch);
  const math::Vec3 forward =
      math::normalize(math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));
  return scenes::SceneTransform{
      .position = math::subtract(
          camera.pivot,
          math::scale(forward, std::max(camera.distance, 0.0f))),
      .rotation = rotation,
  };
}

scenes::SceneTransform composeSceneTransforms(
    const scenes::SceneTransform& parent,
    const scenes::SceneTransform& child) {
  return scenes::SceneTransform{
      .position = math::add(
          parent.position,
          math::rotateVec(parent.rotation,
                          math::multiply(child.position, parent.scale))),
      .rotation = math::mul(parent.rotation, child.rotation),
      .scale = math::multiply(parent.scale, child.scale),
  };
}

scenes::SceneTransform sceneTransformRelativeTo(
    const scenes::SceneTransform& parent,
    const scenes::SceneTransform& composed) {
  const auto divide = [](float value, float divisor) {
    return std::abs(divisor) > 1.0e-6f ? value / divisor : value;
  };
  const auto divide_scale = [&](const math::Vec3& numerator,
                                const math::Vec3& denominator) {
    return math::Vec3{divide(numerator.x, denominator.x),
                      divide(numerator.y, denominator.y),
                      divide(numerator.z, denominator.z)};
  };

  scenes::SceneTransform child{};
  child.scale = divide_scale(composed.scale, parent.scale);
  child.rotation = math::mul(math::inverse(parent.rotation), composed.rotation);
  const math::Vec3 offset = math::rotateVec(
      math::inverse(parent.rotation),
      math::subtract(composed.position, parent.position));
  child.position = divide_scale(offset, parent.scale);
  return child;
}

scenes::SceneTransform sceneTransformWithoutChild(
    const scenes::SceneTransform& composed,
    const scenes::SceneTransform& child) {
  const auto divide = [](float value, float divisor) {
    return std::abs(divisor) > 1.0e-6f ? value / divisor : value;
  };
  scenes::SceneTransform parent{};
  parent.scale = {divide(composed.scale.x, child.scale.x),
                  divide(composed.scale.y, child.scale.y),
                  divide(composed.scale.z, child.scale.z)};
  parent.rotation = math::mul(composed.rotation, math::inverse(child.rotation));
  parent.position = math::subtract(
      composed.position,
      math::rotateVec(parent.rotation,
                      math::multiply(child.position, parent.scale)));
  return parent;
}

bool shouldResolveViewportSelection(bool selection_pending,
                                    bool gizmo_active,
                                    bool gizmo_hovered) {
  return selection_pending && !gizmo_active && !gizmo_hovered;
}

HierarchyBuildResult buildHierarchy(const scenes::SceneDocument& document) {
  HierarchyBuildResult result{};
  std::unordered_map<std::string, const scenes::SceneEntity*> entities;
  entities.reserve(document.entities.size());
  for (const scenes::SceneEntity& entity : document.entities) {
    if (entity.id.empty()) {
      result.diagnostics.push_back("hierarchy contains an entity with an empty id");
      continue;
    }
    if (!entities.emplace(entity.id, &entity).second) {
      result.diagnostics.push_back("hierarchy contains duplicate item id: " +
                                   entity.id);
    }
  }

  std::unordered_map<std::string, std::vector<std::string>> entity_children;
  std::unordered_map<std::string, std::vector<const scenes::ScenePrefabInstance*>>
      prefab_children;
  std::vector<std::string> root_entities;
  std::vector<const scenes::ScenePrefabInstance*> root_prefabs;
  for (const scenes::SceneEntity& entity : document.entities) {
    if (entity.id.empty() || entities[entity.id] != &entity) {
      continue;
    }
    if (entity.parent_id.empty()) {
      root_entities.push_back(entity.id);
    } else if (entities.contains(entity.parent_id)) {
      entity_children[entity.parent_id].push_back(entity.id);
    } else {
      result.diagnostics.push_back("hierarchy entity '" + entity.id +
                                   "' has missing parent: " + entity.parent_id);
      root_entities.push_back(entity.id);
    }
  }

  std::unordered_set<std::string> item_ids;
  for (const auto& [id, entity] : entities) {
    (void)entity;
    item_ids.insert(id);
  }
  for (const scenes::ScenePrefabInstance& prefab : document.prefab_instances) {
    if (prefab.id.empty()) {
      result.diagnostics.push_back("hierarchy contains a prefab with an empty id");
      continue;
    }
    if (!item_ids.insert(prefab.id).second) {
      result.diagnostics.push_back("hierarchy contains duplicate item id: " +
                                   prefab.id);
      continue;
    }
    if (prefab.parent_entity_id.empty()) {
      root_prefabs.push_back(&prefab);
    } else if (entities.contains(prefab.parent_entity_id)) {
      prefab_children[prefab.parent_entity_id].push_back(&prefab);
    } else {
      result.diagnostics.push_back("hierarchy prefab '" + prefab.id +
                                   "' has missing parent: " +
                                   prefab.parent_entity_id);
      root_prefabs.push_back(&prefab);
    }
  }

  std::unordered_map<std::string, uint8_t> visit_state;
  std::function<std::optional<HierarchyNode>(const std::string&)> visit =
      [&](const std::string& id) -> std::optional<HierarchyNode> {
    const uint8_t state = visit_state[id];
    if (state == 1u) {
      result.diagnostics.push_back("hierarchy contains a cycle at: " + id);
      return std::nullopt;
    }
    if (state == 2u) {
      return std::nullopt;
    }
    visit_state[id] = 1u;
    HierarchyNode node{.item = {SelectionKind::Entity, id}};
    if (const auto children = entity_children.find(id);
        children != entity_children.end()) {
      for (const std::string& child_id : children->second) {
        if (auto child = visit(child_id)) {
          node.children.push_back(std::move(*child));
        }
      }
    }
    if (const auto prefabs = prefab_children.find(id);
        prefabs != prefab_children.end()) {
      for (const scenes::ScenePrefabInstance* prefab : prefabs->second) {
        node.children.push_back(
            HierarchyNode{.item = {SelectionKind::Prefab, prefab->id}});
      }
    }
    visit_state[id] = 2u;
    return node;
  };

  for (const std::string& id : root_entities) {
    if (auto root = visit(id)) {
      result.roots.push_back(std::move(*root));
    }
  }
  for (const scenes::SceneEntity& entity : document.entities) {
    if (!entity.id.empty() && visit_state[entity.id] == 0u) {
      if (auto root = visit(entity.id)) {
        result.roots.push_back(std::move(*root));
      }
    }
  }
  for (const scenes::ScenePrefabInstance* prefab : root_prefabs) {
    result.roots.push_back(
        HierarchyNode{.item = {SelectionKind::Prefab, prefab->id}});
  }
  return result;
}

HierarchyBuildResult projectFoliageUnderTerrain(
    HierarchyBuildResult hierarchy,
    std::string_view terrain_entity_id,
    const std::vector<std::string>& foliage_entity_ids) {
  if (terrain_entity_id.empty() || foliage_entity_ids.empty()) {
    return hierarchy;
  }
  const std::unordered_set<std::string> foliage_ids(
      foliage_entity_ids.begin(), foliage_entity_ids.end());
  if (foliage_ids.contains(std::string(terrain_entity_id))) {
    return hierarchy;
  }

  const auto contains = [&](const auto& self,
                            const std::vector<HierarchyNode>& nodes,
                            std::string_view id) -> bool {
    for (const HierarchyNode& node : nodes) {
      if (node.item.kind == SelectionKind::Entity && node.item.id == id) {
        return true;
      }
      if (self(self, node.children, id)) return true;
    }
    return false;
  };
  if (!contains(contains, hierarchy.roots, terrain_entity_id)) {
    return hierarchy;
  }

  std::vector<HierarchyNode> extracted;
  const auto extract = [&](const auto& self,
                           std::vector<HierarchyNode>& nodes) -> void {
    for (size_t index = 0u; index < nodes.size();) {
      HierarchyNode& node = nodes[index];
      if (node.item.kind == SelectionKind::Entity &&
          foliage_ids.contains(node.item.id)) {
        extracted.push_back(std::move(node));
        nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(index));
        continue;
      }
      self(self, node.children);
      ++index;
    }
  };
  extract(extract, hierarchy.roots);
  if (extracted.empty()) return hierarchy;

  const auto append = [&](const auto& self,
                          std::vector<HierarchyNode>& nodes) -> bool {
    for (HierarchyNode& node : nodes) {
      if (node.item.kind == SelectionKind::Entity &&
          node.item.id == terrain_entity_id) {
        for (HierarchyNode& foliage : extracted) {
          node.children.push_back(std::move(foliage));
        }
        return true;
      }
      if (self(self, node.children)) return true;
    }
    return false;
  };
  if (!append(append, hierarchy.roots)) {
    for (HierarchyNode& foliage : extracted) {
      hierarchy.roots.push_back(std::move(foliage));
    }
  }
  return hierarchy;
}

std::optional<scenes::SceneTransform> sceneWorldTransform(
    const scenes::SceneDocument& document,
    const Selection& item,
    std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  scenes::SceneTransform transform{};
  std::string parent_id;
  if (item.kind == SelectionKind::Entity) {
    const auto entity = std::find_if(
        document.entities.begin(), document.entities.end(),
        [&](const scenes::SceneEntity& value) { return value.id == item.id; });
    if (entity == document.entities.end()) {
      fail(diagnostic, "hierarchy entity does not exist: " + item.id);
      return std::nullopt;
    }
    transform = entity->transform;
    parent_id = entity->parent_id;
  } else if (item.kind == SelectionKind::Prefab) {
    const auto prefab = std::find_if(
        document.prefab_instances.begin(), document.prefab_instances.end(),
        [&](const scenes::ScenePrefabInstance& value) {
          return value.id == item.id;
        });
    if (prefab == document.prefab_instances.end()) {
      fail(diagnostic, "hierarchy prefab does not exist: " + item.id);
      return std::nullopt;
    }
    transform = prefab->transform;
    parent_id = prefab->parent_entity_id;
  } else {
    fail(diagnostic, "hierarchy selection is empty");
    return std::nullopt;
  }

  std::unordered_set<std::string> visited;
  while (!parent_id.empty()) {
    if (!visited.insert(parent_id).second) {
      fail(diagnostic, "hierarchy contains a cycle at: " + parent_id);
      return std::nullopt;
    }
    const auto parent = std::find_if(
        document.entities.begin(), document.entities.end(),
        [&](const scenes::SceneEntity& value) { return value.id == parent_id; });
    if (parent == document.entities.end()) {
      fail(diagnostic, "hierarchy parent does not exist: " + parent_id);
      return std::nullopt;
    }
    transform = composeSceneTransforms(parent->transform, transform);
    parent_id = parent->parent_id;
  }
  return transform;
}

bool canReparent(const scenes::SceneDocument& document,
                 const Selection& item,
                 std::string_view new_parent_entity_id) {
  if (!item.valid()) return false;
  if (item.kind == SelectionKind::Entity) {
    if (std::none_of(document.entities.begin(), document.entities.end(),
                     [&](const scenes::SceneEntity& entity) {
                       return entity.id == item.id;
                     })) {
      return false;
    }
  } else if (item.kind == SelectionKind::Prefab) {
    if (std::none_of(document.prefab_instances.begin(),
                     document.prefab_instances.end(),
                     [&](const scenes::ScenePrefabInstance& prefab) {
                       return prefab.id == item.id;
                     })) {
      return false;
    }
  } else {
    return false;
  }
  if (item.kind == SelectionKind::Entity) {
    const auto belongs_to_subtree = [&](const std::string& entity_id) {
      std::string cursor = entity_id;
      std::unordered_set<std::string> visited;
      while (!cursor.empty() && visited.insert(cursor).second) {
        if (cursor == item.id) return true;
        const auto entity = std::find_if(
            document.entities.begin(), document.entities.end(),
            [&](const scenes::SceneEntity& value) {
              return value.id == cursor;
            });
        if (entity == document.entities.end()) return false;
        cursor = entity->parent_id;
      }
      return false;
    };
    if (std::any_of(document.cameras.begin(), document.cameras.end(),
                    [&](const scenes::SceneCamera& camera) {
                      return belongs_to_subtree(camera.entity_id);
                    }) ||
        std::any_of(document.static_components.begin(),
                    document.static_components.end(),
                    [&](const scenes::SceneStaticComponent& component) {
                      return belongs_to_subtree(component.entity_id);
                    }) ||
        std::any_of(document.bakes.begin(), document.bakes.end(),
                    [&](const scenes::SceneBakeDesc& bake) {
                      return belongs_to_subtree(
                          bake.baked_lighting.entity_id);
                    })) {
      return false;
    }
  }
  if (new_parent_entity_id.empty()) return true;

  std::string cursor(new_parent_entity_id);
  std::unordered_set<std::string> visited;
  while (!cursor.empty()) {
    if (item.kind == SelectionKind::Entity && cursor == item.id) return false;
    if (!visited.insert(cursor).second) return false;
    const auto parent = std::find_if(
        document.entities.begin(), document.entities.end(),
        [&](const scenes::SceneEntity& entity) { return entity.id == cursor; });
    if (parent == document.entities.end()) return false;
    cursor = parent->parent_id;
  }
  return true;
}

bool reparentPreservingWorld(scenes::SceneDocument& document,
                             const Selection& item,
                             std::string new_parent_entity_id,
                             std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!canReparent(document, item, new_parent_entity_id)) {
    return fail(diagnostic, "selected item cannot be parented there");
  }

  std::string current_parent;
  if (item.kind == SelectionKind::Entity) {
    const auto entity = std::find_if(
        document.entities.begin(), document.entities.end(),
        [&](const scenes::SceneEntity& value) { return value.id == item.id; });
    current_parent = entity->parent_id;
  } else {
    const auto prefab = std::find_if(
        document.prefab_instances.begin(), document.prefab_instances.end(),
        [&](const scenes::ScenePrefabInstance& value) {
          return value.id == item.id;
        });
    current_parent = prefab->parent_entity_id;
  }
  if (current_parent == new_parent_entity_id) {
    return fail(diagnostic, "selected item already has that parent");
  }

  const std::optional<scenes::SceneTransform> world_transform =
      sceneWorldTransform(document, item, diagnostic);
  if (!world_transform) return false;
  scenes::SceneTransform local = *world_transform;
  if (!new_parent_entity_id.empty()) {
    const auto parent_world = sceneWorldTransform(
        document,
        Selection{SelectionKind::Entity, new_parent_entity_id},
        diagnostic);
    if (!parent_world) return false;
    if (std::abs(parent_world->scale.x) <= 1.0e-6f ||
        std::abs(parent_world->scale.y) <= 1.0e-6f ||
        std::abs(parent_world->scale.z) <= 1.0e-6f) {
      return fail(diagnostic,
                  "new parent has a zero scale and cannot preserve world placement");
    }
    local = sceneTransformRelativeTo(*parent_world, *world_transform);
  }

  if (item.kind == SelectionKind::Entity) {
    const auto entity = std::find_if(
        document.entities.begin(), document.entities.end(),
        [&](const scenes::SceneEntity& value) { return value.id == item.id; });
    entity->parent_id = std::move(new_parent_entity_id);
    entity->transform = local;
  } else {
    const auto prefab = std::find_if(
        document.prefab_instances.begin(), document.prefab_instances.end(),
        [&](const scenes::ScenePrefabInstance& value) {
          return value.id == item.id;
        });
    prefab->parent_entity_id = std::move(new_parent_entity_id);
    prefab->transform = local;
  }
  return true;
}

bool deleteSelectionPreservingWorld(scenes::SceneDocument& document,
                                    const Selection& item,
                                    std::string* diagnostic) {
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!item.valid()) {
    return fail(diagnostic, "cannot delete an empty selection");
  }

  scenes::SceneDocument candidate = document;
  if (item.kind == SelectionKind::Prefab) {
    const size_t before = candidate.prefab_instances.size();
    std::erase_if(candidate.prefab_instances,
                  [&](const scenes::ScenePrefabInstance& prefab) {
                    return prefab.id == item.id;
                  });
    if (candidate.prefab_instances.size() == before) {
      return fail(diagnostic, "selected prefab does not exist");
    }
  } else if (item.kind == SelectionKind::Entity) {
    const auto selected = std::find_if(
        candidate.entities.begin(), candidate.entities.end(),
        [&](const scenes::SceneEntity& entity) { return entity.id == item.id; });
    if (selected == candidate.entities.end()) {
      return fail(diagnostic, "selected entity does not exist");
    }
    if (selected->parent_id.empty()) {
      return fail(diagnostic, "the scene root cannot be deleted");
    }
    if (std::any_of(candidate.cameras.begin(), candidate.cameras.end(),
                    [&](const scenes::SceneCamera& camera) {
                      return camera.entity_id == item.id;
                    })) {
      return fail(diagnostic,
                  "camera entities are preserved by the scene editor");
    }
    if (std::any_of(candidate.static_components.begin(),
                    candidate.static_components.end(),
                    [&](const scenes::SceneStaticComponent& component) {
                      return component.entity_id == item.id;
                    }) ||
        std::any_of(candidate.bakes.begin(), candidate.bakes.end(),
                    [&](const scenes::SceneBakeDesc& bake) {
                      return bake.baked_lighting.entity_id == item.id;
                    })) {
      return fail(
          diagnostic,
          "this entity is referenced by static or baked data and cannot be deleted");
    }

    const std::string promoted_parent = selected->parent_id;
    std::vector<std::string> child_entities;
    std::vector<std::string> child_prefabs;
    for (const scenes::SceneEntity& entity : candidate.entities) {
      if (entity.parent_id == item.id) child_entities.push_back(entity.id);
    }
    for (const scenes::ScenePrefabInstance& prefab : candidate.prefab_instances) {
      if (prefab.parent_entity_id == item.id) child_prefabs.push_back(prefab.id);
    }
    for (const std::string& child_id : child_entities) {
      std::string error;
      if (!reparentPreservingWorld(candidate,
                                   {SelectionKind::Entity, child_id},
                                   promoted_parent,
                                   &error)) {
        return fail(diagnostic,
                    "cannot preserve child placement while deleting: " + error);
      }
    }
    for (const std::string& prefab_id : child_prefabs) {
      std::string error;
      if (!reparentPreservingWorld(candidate,
                                   {SelectionKind::Prefab, prefab_id},
                                   promoted_parent,
                                   &error)) {
        return fail(diagnostic,
                    "cannot preserve child placement while deleting: " + error);
      }
    }
    std::erase_if(candidate.lights, [&](const scenes::SceneLight& light) {
      return light.entity_id == item.id;
    });
    if (candidate.environment &&
        candidate.environment->entity_id == item.id) {
      candidate.environment.reset();
    }
    std::erase_if(candidate.entities, [&](const scenes::SceneEntity& entity) {
      return entity.id == item.id;
    });
  } else {
    return fail(diagnostic, "cannot delete an empty selection");
  }

  const scenes::SceneValidationResult validation =
      scenes::validateSceneDocument(candidate);
  if (!validation.success()) {
    std::string message = "delete would create an invalid scene";
    for (const std::string& entry : validation.diagnostics) {
      message += "\n" + entry;
    }
    return fail(diagnostic, std::move(message));
  }
  document = std::move(candidate);
  return true;
}

std::optional<Selection> duplicateSelection(
    scenes::SceneDocument& document,
    const Selection& item,
    const StableIdGenerator& generate_id,
    std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!item.valid() || !generate_id) {
    fail(diagnostic, "cannot duplicate an empty selection");
    return std::nullopt;
  }

  scenes::SceneDocument candidate = document;
  Selection duplicated{};
  if (item.kind == SelectionKind::Prefab) {
    const auto source = std::find_if(
        document.prefab_instances.begin(), document.prefab_instances.end(),
        [&](const scenes::ScenePrefabInstance& prefab) {
          return prefab.id == item.id;
        });
    if (source == document.prefab_instances.end()) {
      fail(diagnostic, "selected prefab does not exist");
      return std::nullopt;
    }
    scenes::ScenePrefabInstance copy = *source;
    copy.id = generate_id("prefab");
    duplicated = {SelectionKind::Prefab, copy.id};
    candidate.prefab_instances.push_back(std::move(copy));
  } else if (item.kind == SelectionKind::Entity) {
    const auto selected = std::find_if(
        document.entities.begin(), document.entities.end(),
        [&](const scenes::SceneEntity& entity) { return entity.id == item.id; });
    if (selected == document.entities.end()) {
      fail(diagnostic, "selected entity does not exist");
      return std::nullopt;
    }
    if (selected->parent_id.empty()) {
      fail(diagnostic, "the scene root cannot be duplicated");
      return std::nullopt;
    }

    std::unordered_set<std::string> subtree{item.id};
    bool added = true;
    while (added) {
      added = false;
      for (const scenes::SceneEntity& entity : document.entities) {
        if (!subtree.contains(entity.id) &&
            subtree.contains(entity.parent_id)) {
          subtree.insert(entity.id);
          added = true;
        }
      }
    }
    const auto protected_entity = [&](const std::string& id) {
      return subtree.contains(id);
    };
    if ((document.environment &&
         protected_entity(document.environment->entity_id)) ||
        std::any_of(document.cameras.begin(), document.cameras.end(),
                    [&](const scenes::SceneCamera& camera) {
                      return protected_entity(camera.entity_id);
                    }) ||
        std::any_of(document.static_components.begin(),
                    document.static_components.end(),
                    [&](const scenes::SceneStaticComponent& component) {
                      return protected_entity(component.entity_id);
                    }) ||
        std::any_of(document.bakes.begin(), document.bakes.end(),
                    [&](const scenes::SceneBakeDesc& bake) {
                      return protected_entity(bake.baked_lighting.entity_id);
                    }) ||
        std::any_of(document.entities.begin(), document.entities.end(),
                    [&](const scenes::SceneEntity& entity) {
                      return subtree.contains(entity.id) &&
                             !entity.components.empty();
                    })) {
      fail(diagnostic,
           "camera, environment, component-bearing, static, or baked subtrees cannot be duplicated");
      return std::nullopt;
    }

    std::unordered_map<std::string, std::string> remapped_entities;
    for (const scenes::SceneEntity& entity : document.entities) {
      if (subtree.contains(entity.id)) {
        remapped_entities[entity.id] = generate_id("entity");
      }
    }
    duplicated = {SelectionKind::Entity, remapped_entities[item.id]};
    for (const scenes::SceneEntity& entity : document.entities) {
      if (!subtree.contains(entity.id)) continue;
      scenes::SceneEntity copy = entity;
      copy.id = remapped_entities[entity.id];
      if (entity.id == item.id) {
        copy.name = copy.name.empty() ? "Copy" : copy.name + " Copy";
      }
      if (const auto parent = remapped_entities.find(entity.parent_id);
          parent != remapped_entities.end()) {
        copy.parent_id = parent->second;
      }
      candidate.entities.push_back(std::move(copy));
    }
    for (const scenes::ScenePrefabInstance& prefab : document.prefab_instances) {
      const auto parent = remapped_entities.find(prefab.parent_entity_id);
      if (parent == remapped_entities.end()) continue;
      scenes::ScenePrefabInstance copy = prefab;
      copy.id = generate_id("prefab");
      copy.parent_entity_id = parent->second;
      candidate.prefab_instances.push_back(std::move(copy));
    }
    for (const scenes::SceneLight& light : document.lights) {
      const auto entity = remapped_entities.find(light.entity_id);
      if (entity == remapped_entities.end()) continue;
      scenes::SceneLight copy = light;
      copy.id = generate_id("light");
      copy.entity_id = entity->second;
      candidate.lights.push_back(std::move(copy));
    }
  } else {
    fail(diagnostic, "cannot duplicate an empty selection");
    return std::nullopt;
  }

  const scenes::SceneValidationResult validation =
      scenes::validateSceneDocument(candidate);
  if (!validation.success()) {
    std::string message = "duplicate would create an invalid scene";
    for (const std::string& entry : validation.diagnostics) {
      message += "\n" + entry;
    }
    fail(diagnostic, std::move(message));
    return std::nullopt;
  }
  document = std::move(candidate);
  return duplicated;
}

bool pathIsWithin(const std::filesystem::path& root,
                  const std::filesystem::path& candidate) {
  const std::filesystem::path normalized_root = weakCanonical(root);
  const std::filesystem::path normalized_candidate = weakCanonical(candidate);
  auto root_it = normalized_root.begin();
  auto candidate_it = normalized_candidate.begin();
  for (; root_it != normalized_root.end(); ++root_it, ++candidate_it) {
    if (candidate_it == normalized_candidate.end() || *root_it != *candidate_it) {
      return false;
    }
  }
  return true;
}

std::optional<std::filesystem::path> contentRelativePath(
    const std::filesystem::path& content_root,
    const std::filesystem::path& candidate) {
  if (!pathIsWithin(content_root, candidate)) {
    return std::nullopt;
  }
  std::error_code ec;
  std::filesystem::path relative =
      std::filesystem::relative(weakCanonical(candidate), weakCanonical(content_root), ec);
  if (ec || relative.empty() || relative.is_absolute()) {
    return std::nullopt;
  }
  return relative.lexically_normal();
}

std::optional<TerrainCreationResult> createTerrainTransaction(
    const scenes::SceneDocument& source,
    TerrainCreationRequest request,
    scene_authoring::TerrainCanvas canvas,
    std::string* diagnostic) {
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!canvas.valid()) {
    fail(diagnostic, "terrain canvas is invalid");
    return std::nullopt;
  }
  if (request.content_root.empty() || request.preview_directory.empty()) {
    fail(diagnostic, "terrain content root and preview directory are required");
    return std::nullopt;
  }
  if (request.entity_id.empty() ||
      !std::all_of(request.entity_id.begin(), request.entity_id.end(),
                   [](unsigned char character) {
                     return std::isalnum(character) != 0 || character == '_' ||
                            character == '-';
                   })) {
    fail(diagnostic, "terrain entity id is empty or contains unsafe path characters");
    return std::nullopt;
  }
  if (!std::isfinite(canvas.desc().terrain_size) ||
      !std::isfinite(canvas.desc().height_scale) ||
      !std::isfinite(canvas.desc().height_offset) ||
      !std::isfinite(canvas.desc().terrain_size * 2.0f)) {
    fail(diagnostic, "terrain dimensions must be finite");
    return std::nullopt;
  }
  if (std::any_of(source.entities.begin(), source.entities.end(),
                  [&](const scenes::SceneEntity& entity) {
                    return entity.id == request.entity_id;
                  })) {
    fail(diagnostic, "terrain entity id already exists in the scene");
    return std::nullopt;
  }
  if (std::any_of(source.entities.begin(), source.entities.end(),
                  [](const scenes::SceneEntity& entity) {
                    return entity.components.is_object() &&
                           entity.components.contains("TerrainComponent");
                  })) {
    fail(diagnostic, "the scene already contains an editable terrain");
    return std::nullopt;
  }
  if (!request.parent_entity_id.empty() &&
      std::none_of(source.entities.begin(), source.entities.end(),
                   [&](const scenes::SceneEntity& entity) {
                     return entity.id == request.parent_entity_id;
                   })) {
    fail(diagnostic, "terrain parent entity does not exist");
    return std::nullopt;
  }

  if (request.preview_directory.is_relative()) {
    request.preview_directory = request.content_root / request.preview_directory;
  }
  request.preview_directory = request.preview_directory.lexically_normal();
  const std::filesystem::path height_path =
      request.preview_directory / (request.entity_id + "-height.r32");
  const std::filesystem::path control_path =
      request.preview_directory / (request.entity_id + "-control.tga");
  const auto relative_height =
      contentRelativePath(request.content_root, height_path);
  const auto relative_control =
      contentRelativePath(request.content_root, control_path);
  if (!relative_height || !relative_control) {
    fail(diagnostic, "terrain preview paths must remain inside the content root");
    return std::nullopt;
  }

  components::TerrainComponent component{};
  component.source = components::TerrainSourceType::SingleImage;
  component.height_image = *relative_height;
  component.control_image = *relative_control;
  component.height_format = components::TerrainHeightFormat::R32Float;
  component.raw_width = canvas.resolution();
  component.raw_height = canvas.resolution();
  component.terrain_size = canvas.desc().terrain_size;
  component.tile_resolution = canvas.resolution();
  component.height_scale = canvas.desc().height_scale;
  component.height_offset = canvas.desc().height_offset;
  component.view_distance = canvas.desc().terrain_size * 2.0f;
  for (uint32_t layer = 0u; layer < 4u; ++layer) {
    component.material_layers.push_back(components::TerrainMaterialLayer{
        .name = "Layer " + std::to_string(layer + 1u),
        .enabled = false,
    });
  }

  Json component_json;
  try {
    prefabs::ensureBuiltinComponentSerializers();
    const prefabs::ComponentSerializer* serializer =
        prefabs::componentSerializerRegistry().find("TerrainComponent");
    if (serializer == nullptr) {
      fail(diagnostic, "terrain component serializer is unavailable");
      return std::nullopt;
    }
    world::World temporary_world;
    const world::Entity temporary_entity = temporary_world.createEntity();
    temporary_world.add(temporary_entity, component);
    component_json = serializer->serialize(temporary_world, temporary_entity);
  } catch (const std::exception& error) {
    fail(diagnostic,
         std::string("failed to serialize terrain component: ") + error.what());
    return std::nullopt;
  }

  scenes::SceneDocument next;
  try {
    next = source;
    next.entities.push_back(scenes::SceneEntity{
        .id = request.entity_id,
        .name = "Terrain",
        .parent_id = request.parent_entity_id,
        .transform = scenes::SceneTransform{
            .position = {-canvas.desc().terrain_size * 0.5f,
                         0.0f,
                         -canvas.desc().terrain_size * 0.5f}},
        .components = Json{{"TerrainComponent", std::move(component_json)}},
    });
  } catch (const std::exception& error) {
    fail(diagnostic,
         std::string("failed to stage terrain document: ") + error.what());
    return std::nullopt;
  }
  const scenes::SceneValidationResult validation =
      scenes::validateSceneDocument(next);
  if (!validation.success()) {
    std::string message = "terrain would create an invalid scene";
    for (const std::string& entry : validation.diagnostics) {
      message += "\n" + entry;
    }
    fail(diagnostic, std::move(message));
    return std::nullopt;
  }

  const std::filesystem::path temporary_height = height_path.string() + ".tmp";
  const std::filesystem::path temporary_control = control_path.string() + ".tmp";
  bool height_committed = false;
  auto cleanup = [&] {
    std::error_code ignored;
    std::filesystem::remove(temporary_height, ignored);
    std::filesystem::remove(temporary_control, ignored);
    if (height_committed) {
      std::filesystem::remove(height_path, ignored);
    }
  };

  std::error_code ec;
  const bool height_exists = std::filesystem::exists(height_path, ec);
  if (ec) {
    fail(diagnostic, "failed to inspect terrain height path: " + ec.message());
    return std::nullopt;
  }
  const bool control_exists = std::filesystem::exists(control_path, ec);
  if (ec) {
    fail(diagnostic, "failed to inspect terrain control path: " + ec.message());
    return std::nullopt;
  }
  if (height_exists || control_exists) {
    fail(diagnostic, "terrain preview output path already exists");
    return std::nullopt;
  }
  std::filesystem::create_directories(request.preview_directory, ec);
  if (ec) {
    fail(diagnostic, "failed to create terrain preview directory: " + ec.message());
    return std::nullopt;
  }

  std::string write_error;
  try {
    if (!canvas.saveHeightR32(temporary_height, &write_error) ||
        !canvas.saveControlTga(temporary_control, &write_error)) {
      cleanup();
      fail(diagnostic,
           write_error.empty() ? "failed to write terrain preview sidecars"
                               : std::move(write_error));
      return std::nullopt;
    }
  } catch (const std::exception& error) {
    cleanup();
    fail(diagnostic,
         std::string("failed to write terrain preview sidecars: ") + error.what());
    return std::nullopt;
  }

  std::filesystem::rename(temporary_height, height_path, ec);
  if (ec) {
    cleanup();
    fail(diagnostic, "failed to commit terrain height sidecar: " + ec.message());
    return std::nullopt;
  }
  height_committed = true;
  ec.clear();
  std::filesystem::rename(temporary_control, control_path, ec);
  if (ec) {
    cleanup();
    fail(diagnostic, "failed to commit terrain control sidecar: " + ec.message());
    return std::nullopt;
  }
  height_committed = false;

  return TerrainCreationResult{
      .document = std::move(next),
      .canvas = std::move(canvas),
      .entity_id = std::move(request.entity_id),
      .height_path = height_path,
      .control_path = control_path,
  };
}

CatalogScanResult AssetCatalog::scan(
    const std::filesystem::path& content_root,
    const std::vector<std::filesystem::path>& asset_roots) {
  CatalogScanResult result{};
  entries_.clear();
  keys_.clear();
  watched_files_.clear();

  const std::filesystem::path canonical_root = weakCanonical(content_root);
  std::unordered_map<std::string, size_t> unique_paths;
  auto watch = [&](const std::filesystem::path& path) {
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (!ec) watched_files_[weakCanonical(path).generic_string()] = modified;
  };
  auto append = [&](AssetEntry entry) {
    const std::string path_key = weakCanonical(entry.path).generic_string() + "#" + entry.key;
    if (!unique_paths.emplace(path_key, entries_.size()).second) {
      return;
    }
    if (!entry.key.empty()) {
      const auto existing = keys_.find(entry.key);
      if (existing != keys_.end()) {
        entry.valid = false;
        entry.diagnostic = "asset key conflicts with " + entries_[existing->second].path.string();
        entries_[existing->second].valid = false;
        entries_[existing->second].diagnostic = "asset key conflicts with " + entry.path.string();
      } else {
        keys_[entry.key] = entries_.size();
      }
    }
    watch(entry.path);
    entries_.push_back(std::move(entry));
  };

  for (const std::filesystem::path& configured_root : asset_roots) {
    std::filesystem::path root = configured_root;
    if (root.is_relative()) {
      root = canonical_root / root;
    }
    root = weakCanonical(root);
    if (!pathIsWithin(canonical_root, root)) {
      result.diagnostics.push_back("asset root is outside the content root: " + root.string());
      continue;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
      result.diagnostics.push_back("asset root is not a directory: " + root.string());
      continue;
    }
    watch(root);

    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
      if (ec) {
        result.diagnostics.push_back("asset scan warning under " + root.string() + ": " + ec.message());
        ec.clear();
        continue;
      }
      if (it->is_directory(ec)) {
        if (!ec) {
          const std::string name = it->path().filename().string();
          if (name == ".git" || name == ".karma" || name == "build" ||
              name.starts_with("cmake-build-")) {
            it.disable_recursion_pending();
          } else {
            watch(it->path());
          }
        }
        continue;
      }
      if (!it->is_regular_file(ec)) {
        continue;
      }
      const std::filesystem::path path = it->path();
      const std::string filename = path.filename().string();
      if (filename != "prefab.json" && filename != "assets.package.json") {
        continue;
      }
      Json json;
      std::string diagnostic;
      const auto modified = std::filesystem::last_write_time(path, ec);
      if (!readJson(path, json, diagnostic)) {
        append(AssetEntry{.kind = filename == "prefab.json" ? AssetKind::Prefab : AssetKind::Package,
                          .name = path.parent_path().filename().string(),
                          .path = path,
                          .modified = modified,
                          .valid = false,
                          .diagnostic = std::move(diagnostic)});
        continue;
      }

      if (!json.is_object()) {
        append(AssetEntry{
            .kind = filename == "prefab.json" ? AssetKind::Prefab : AssetKind::Package,
            .name = path.parent_path().filename().string(),
            .path = path,
            .modified = modified,
            .valid = false,
            .diagnostic = "asset document root must be an object",
        });
        continue;
      }

      if (filename == "prefab.json") {
        const prefabs::PrefabLoadResult loaded =
            prefabs::loadPrefabDocument(path);
        if (!loaded.success()) {
          std::string prefab_diagnostic = "prefab validation failed";
          for (const std::string& entry : loaded.diagnostics) {
            prefab_diagnostic += "\n" + entry;
          }
          append(AssetEntry{.kind = AssetKind::Prefab,
                            .name = prefabDisplayName(json, path),
                            .type = "prefab",
                            .path = path,
                            .modified = modified,
                            .valid = false,
                            .diagnostic = std::move(prefab_diagnostic)});
          continue;
        }
        append(AssetEntry{.kind = AssetKind::Prefab,
                          .name = prefabDisplayName(json, path),
                          .type = "prefab",
                          .path = path,
                          .modified = modified});
        continue;
      }

      const auto assets = json.find("assets");
      std::string schema_diagnostic;
      const auto version = json.find("version");
      if (version == json.end() || !isVersionOne(*version)) {
        schema_diagnostic = "asset package version must be the integer 1";
      } else if (assets == json.end() || !assets->is_array()) {
        schema_diagnostic = "asset package requires an 'assets' array";
      } else {
        for (size_t index = 0u; index < assets->size(); ++index) {
          const Json& item = (*assets)[index];
          if (!item.is_object()) {
            schema_diagnostic = "asset package entry " + std::to_string(index) +
                                " must be an object";
            break;
          }
          const auto type = item.find("type");
          if (type == item.end() || !type->is_string() ||
              type->get_ref<const std::string&>().empty()) {
            schema_diagnostic = "asset package entry " + std::to_string(index) +
                                " requires a non-empty string field 'type'";
            break;
          }
          const auto key = item.find("key");
          if (key == item.end() || !key->is_string() ||
              key->get_ref<const std::string&>().empty()) {
            schema_diagnostic = "asset package entry " + std::to_string(index) +
                                " requires a non-empty string field 'key'";
            break;
          }
          const auto source = item.find("path");
          if (source != item.end() && !source->is_string()) {
            schema_diagnostic = "asset package entry " + std::to_string(index) +
                                " field 'path' must be a string";
            break;
          }
        }
      }
      if (!schema_diagnostic.empty()) {
        append(AssetEntry{.kind = AssetKind::Package,
                          .name = path.parent_path().filename().string(),
                          .type = "asset_package",
                          .path = path,
                          .package_path = path,
                          .modified = modified,
                          .valid = false,
                          .diagnostic = std::move(schema_diagnostic)});
        continue;
      }

      append(AssetEntry{.kind = AssetKind::Package,
                        .name = path.parent_path().filename().string(),
                        .type = "asset_package",
                        .path = path,
                        .package_path = path,
                        .modified = modified});
      if (assets == json.end() || !assets->is_array()) {
        continue;
      }
      for (const Json& item : *assets) {
        const auto type_it = item.find("type");
        const auto key_it = item.find("key");
        const std::string type =
            type_it == item.end() ? std::string{} : type_it->get<std::string>();
        const std::string key =
            key_it == item.end() ? std::string{} : key_it->get<std::string>();
        if (key.empty()) continue;
        std::filesystem::path source_path = path;
        const auto source_it = item.find("path");
        if (source_it != item.end() && source_it->is_string()) {
          const std::filesystem::path source =
              weakCanonical(path.parent_path() /
                            std::filesystem::path(source_it->get<std::string>()));
          if (pathIsWithin(canonical_root, source)) {
            source_path = source;
            watch(source);
          }
        }
        append(AssetEntry{.kind = kindFromPackageType(type),
                          .name = key.substr(key.find_last_of('/') == std::string::npos
                                                ? 0u
                                                : key.find_last_of('/') + 1u),
                          .key = key,
                          .type = type,
                          .path = source_path,
                          .package_path = path,
                          .modified = modified});
      }
    }
  }

  std::sort(entries_.begin(), entries_.end(), [](const AssetEntry& a, const AssetEntry& b) {
    if (a.kind != b.kind) return a.kind < b.kind;
    if (a.name != b.name) return a.name < b.name;
    return a.path.generic_string() < b.path.generic_string();
  });
  keys_.clear();
  for (size_t index = 0u; index < entries_.size(); ++index) {
    if (!entries_[index].key.empty() && !keys_.contains(entries_[index].key)) {
      keys_[entries_[index].key] = index;
    }
  }
  result.entries = entries_;
  return result;
}

std::vector<std::filesystem::path> AssetCatalog::changedFiles() const {
  std::vector<std::filesystem::path> changed;
  for (const auto& [path_string, previous] : watched_files_) {
    std::error_code ec;
    const std::filesystem::path path(path_string);
    const auto current = std::filesystem::last_write_time(path, ec);
    if (ec || current != previous) {
      changed.push_back(path);
    }
  }
  return changed;
}

const AssetEntry* AssetCatalog::findByKey(std::string_view key) const {
  const auto it = keys_.find(std::string(key));
  return it == keys_.end() ? nullptr : &entries_[it->second];
}

const AssetEntry* AssetCatalog::findPrefab(const std::filesystem::path& path) const {
  const std::filesystem::path normalized = weakCanonical(path);
  const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const AssetEntry& entry) {
    return entry.kind == AssetKind::Prefab && weakCanonical(entry.path) == normalized;
  });
  return it == entries_.end() ? nullptr : &*it;
}

EditorWorkspaceLayout resolveEditorWorkspaceLayout(
    const EditorSettings::PanelLayout& preferred,
    float workspace_width,
    float workspace_height) {
  constexpr float splitter = 6.0f;
  constexpr float hierarchy_min = 220.0f;
  constexpr float center_min = 320.0f;
  constexpr float inspector_min = 300.0f;
  constexpr float viewport_min = 220.0f;
  constexpr float assets_min = 150.0f;

  const auto extent = [](float value) {
    return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
  };
  const auto preference = [](float value, float fallback) {
    return std::isfinite(value) && value > 0.0f ? value : fallback;
  };

  const float width = extent(workspace_width);
  const float horizontal = std::max(width - splitter * 2.0f, 0.0f);
  const float nominal_horizontal_min =
      hierarchy_min + center_min + inspector_min;
  const float horizontal_scale =
      nominal_horizontal_min > 0.0f
          ? std::min(1.0f, horizontal / nominal_horizontal_min)
          : 1.0f;
  const float effective_hierarchy_min = hierarchy_min * horizontal_scale;
  const float effective_center_min = center_min * horizontal_scale;
  const float effective_inspector_min = inspector_min * horizontal_scale;

  float hierarchy = std::max(
      preference(preferred.hierarchy_width, EditorSettings{}.panel_layout.hierarchy_width),
      effective_hierarchy_min);
  float inspector = std::max(
      preference(preferred.inspector_width, EditorSettings{}.panel_layout.inspector_width),
      effective_inspector_min);
  const float maximum_side_total =
      std::max(horizontal - effective_center_min, 0.0f);
  if (hierarchy + inspector > maximum_side_total) {
    const float hierarchy_extra =
        std::max(hierarchy - effective_hierarchy_min, 0.0f);
    const float inspector_extra =
        std::max(inspector - effective_inspector_min, 0.0f);
    const float available_extra = std::max(
        maximum_side_total - effective_hierarchy_min - effective_inspector_min,
        0.0f);
    const float requested_extra = hierarchy_extra + inspector_extra;
    if (requested_extra > 0.0f) {
      hierarchy = effective_hierarchy_min +
                  available_extra * hierarchy_extra / requested_extra;
      inspector = effective_inspector_min +
                  available_extra * inspector_extra / requested_extra;
    } else {
      hierarchy = effective_hierarchy_min;
      inspector = std::max(maximum_side_total - hierarchy, 0.0f);
    }
  }
  const float center = std::max(horizontal - hierarchy - inspector, 0.0f);

  const float height = extent(workspace_height);
  const float vertical = std::max(height - splitter, 0.0f);
  const float nominal_vertical_min = viewport_min + assets_min;
  const float vertical_scale =
      nominal_vertical_min > 0.0f
          ? std::min(1.0f, vertical / nominal_vertical_min)
          : 1.0f;
  const float effective_viewport_min = viewport_min * vertical_scale;
  const float effective_assets_min = assets_min * vertical_scale;
  const float requested_assets = std::max(
      preference(preferred.assets_height, EditorSettings{}.panel_layout.assets_height),
      effective_assets_min);
  const float assets = std::clamp(
      requested_assets,
      effective_assets_min,
      std::max(vertical - effective_viewport_min, effective_assets_min));
  const float viewport = std::max(vertical - assets, 0.0f);

  return EditorWorkspaceLayout{
      .hierarchy_width = hierarchy,
      .inspector_width = inspector,
      .center_width = center,
      .viewport_height = viewport,
      .assets_height = assets,
      .splitter_size = splitter,
      .compact_width = horizontal_scale < 1.0f,
      .compact_height = vertical_scale < 1.0f,
  };
}

bool blocksViewportPointerInput(const EditorPointerCaptureState& state) {
  if (state.popup_open || state.drag_drop_active) return true;
  if (state.viewport_navigation_owned) return false;
  return state.panel_item_active ||
         (state.want_capture_mouse && !state.viewport_item_hovered);
}

std::filesystem::path settingsPath(const std::filesystem::path& content_root) {
  return content_root / ".karma" / "scene-editor.local.json";
}

bool loadEditorSettings(const std::filesystem::path& content_root,
                        EditorSettings& settings,
                        std::string* diagnostic) {
  const std::filesystem::path path = settingsPath(content_root);
  if (!std::filesystem::exists(path)) {
    settings = EditorSettings{.asset_roots = {"."}};
    return true;
  }
  Json json;
  std::string error;
  if (!readJson(path, json, error) || !json.is_object()) {
    return fail(diagnostic, error.empty() ? "editor settings root must be an object" : error);
  }
  EditorSettings loaded{};
  const auto version = json.find("version");
  if (version != json.end()) {
    if (!isVersionOne(*version)) {
      return fail(diagnostic, "editor settings version must be the integer 1");
    }
  }
  const auto roots = json.find("asset_roots");
  if (roots != json.end()) {
    if (!roots->is_array()) return fail(diagnostic, "editor asset_roots must be an array");
    for (const Json& value : *roots) {
      if (!value.is_string()) return fail(diagnostic, "editor asset root must be a string");
      const std::filesystem::path root(value.get<std::string>());
      if (root.is_absolute() || root.generic_string().find("..") != std::string::npos) {
        return fail(diagnostic, "editor asset roots must be portable content-root-relative paths");
      }
      loaded.asset_roots.push_back(root);
    }
  }
  const auto camera_move_speed = json.find("camera_move_speed");
  if (camera_move_speed != json.end()) {
    if (!camera_move_speed->is_number()) {
      return fail(diagnostic, "editor camera_move_speed must be a number");
    }
    loaded.camera_move_speed = camera_move_speed->get<float>();
    if (!std::isfinite(loaded.camera_move_speed) || loaded.camera_move_speed <= 0.0f) {
      return fail(diagnostic, "editor camera_move_speed must be finite and positive");
    }
  }
  const auto grid_size = json.find("grid_size");
  if (grid_size != json.end()) {
    if (!grid_size->is_number()) {
      return fail(diagnostic, "editor grid_size must be a number");
    }
    loaded.grid_size = grid_size->get<float>();
    if (!std::isfinite(loaded.grid_size) || loaded.grid_size <= 0.0f) {
      return fail(diagnostic, "editor grid_size must be finite and positive");
    }
  }
  const auto snap_enabled = json.find("snap_enabled");
  if (snap_enabled != json.end()) {
    if (!snap_enabled->is_boolean()) {
      return fail(diagnostic, "editor snap_enabled must be a boolean");
    }
    loaded.snap_enabled = snap_enabled->get<bool>();
  }
  const auto markers_visible = json.find("markers_visible");
  if (markers_visible != json.end()) {
    if (!markers_visible->is_boolean()) {
      return fail(diagnostic, "editor markers_visible must be a boolean");
    }
    loaded.markers_visible = markers_visible->get<bool>();
  }
  const auto layout = json.find("layout");
  if (layout != json.end()) {
    if (!layout->is_object()) {
      return fail(diagnostic, "editor layout must be an object");
    }
    const auto read_dimension = [&](std::string_view name, float& value) {
      const auto field = layout->find(std::string(name));
      if (field == layout->end()) return true;
      if (!field->is_number()) {
        return fail(diagnostic, "editor layout " + std::string(name) +
                                    " must be a number");
      }
      value = field->get<float>();
      if (!std::isfinite(value) || value <= 0.0f) {
        return fail(diagnostic, "editor layout " + std::string(name) +
                                    " must be finite and positive");
      }
      return true;
    };
    if (!read_dimension("hierarchy_width", loaded.panel_layout.hierarchy_width) ||
        !read_dimension("inspector_width", loaded.panel_layout.inspector_width) ||
        !read_dimension("assets_height", loaded.panel_layout.assets_height)) {
      return false;
    }
  }
  const auto asset_filter = json.find("asset_filter");
  if (asset_filter != json.end()) {
    if (!asset_filter->is_string()) {
      return fail(diagnostic, "editor asset_filter must be a string");
    }
    loaded.asset_filter = asset_filter->get<std::string>();
    if (loaded.asset_filter.size() > 191u) {
      return fail(diagnostic, "editor asset_filter must contain at most 191 bytes");
    }
  }
  const auto read_short_string = [&](std::string_view name,
                                     std::string& value,
                                     size_t maximum_size) {
    const auto field = json.find(std::string(name));
    if (field == json.end()) return true;
    if (!field->is_string()) {
      return fail(diagnostic, "editor " + std::string(name) +
                                  " must be a string");
    }
    value = field->get<std::string>();
    if (value.size() > maximum_size) {
      return fail(diagnostic, "editor " + std::string(name) +
                                  " is too long");
    }
    return true;
  };
  if (!read_short_string("hierarchy_filter", loaded.hierarchy_filter, 127u) ||
      !read_short_string("inspector_filter", loaded.inspector_filter, 127u) ||
      !read_short_string("selected_bake_id", loaded.selected_bake_id, 191u) ||
      !read_short_string("active_foliage_layer_id",
                         loaded.active_foliage_layer_id, 191u)) {
    return false;
  }
  const auto component_foldouts = json.find("component_foldouts");
  if (component_foldouts != json.end()) {
    if (!component_foldouts->is_object()) {
      return fail(diagnostic, "editor component_foldouts must be an object");
    }
    if (component_foldouts->size() > 256u) {
      return fail(diagnostic,
                  "editor component_foldouts contains too many entries");
    }
    for (auto entry = component_foldouts->begin();
         entry != component_foldouts->end(); ++entry) {
      if (entry.key().empty() || entry.key().size() > 191u ||
          !entry.value().is_boolean()) {
        return fail(diagnostic,
                    "editor component_foldouts entries must map short names "
                    "to booleans");
      }
      loaded.component_foldouts.emplace(entry.key(),
                                        entry.value().get<bool>());
    }
  }
  const auto read_bounded_integer = [&](std::string_view name,
                                        int minimum,
                                        int maximum,
                                        int& value) {
    const auto field = json.find(std::string(name));
    if (field == json.end()) return true;
    if (!field->is_number_integer()) {
      return fail(diagnostic, "editor " + std::string(name) +
                                  " must be an integer");
    }
    const int parsed = field->get<int>();
    if (parsed < minimum || parsed > maximum) {
      return fail(diagnostic, "editor " + std::string(name) +
                                  " is out of range");
    }
    value = parsed;
    return true;
  };
  if (!read_bounded_integer("asset_type_filter", 0, 6,
                            loaded.asset_type_filter) ||
      !read_bounded_integer("console_min_level", 0, 5,
                            loaded.console_min_level) ||
      !read_bounded_integer("terrain_inspector_tab", 0, 2,
                            loaded.terrain_inspector_tab) ||
      !read_bounded_integer("terrain_material_layer", 0, 3,
                            loaded.terrain_material_layer)) {
    return false;
  }
  const auto bottom_panel_tab = json.find("bottom_panel_tab");
  if (bottom_panel_tab != json.end()) {
    if (!bottom_panel_tab->is_string()) {
      return fail(diagnostic, "editor bottom_panel_tab must be a string");
    }
    const std::string value = bottom_panel_tab->get<std::string>();
    if (value == "assets") {
      loaded.bottom_panel_tab = BottomPanelTab::Assets;
    } else if (value == "console") {
      loaded.bottom_panel_tab = BottomPanelTab::Console;
    } else if (value == "lighting") {
      loaded.bottom_panel_tab = BottomPanelTab::Lighting;
    } else if (value == "navigation") {
      loaded.bottom_panel_tab = BottomPanelTab::Navigation;
    } else {
      return fail(diagnostic,
                  "editor bottom_panel_tab must be one of assets, console, "
                  "lighting, or navigation");
    }
  }
  const auto viewport_render_mode = json.find("viewport_render_mode");
  if (viewport_render_mode != json.end()) {
    if (!viewport_render_mode->is_string()) {
      return fail(diagnostic,
                  "editor viewport_render_mode must be a string");
    }
    const auto parsed =
        parseViewportRenderMode(viewport_render_mode->get<std::string>());
    if (!parsed.has_value()) {
      return fail(diagnostic,
                  "editor viewport_render_mode must be one of rendered, "
                  "diffuse, texture, or wire");
    }
    loaded.viewport_render_mode = *parsed;
  }
  if (loaded.asset_roots.empty()) loaded.asset_roots.push_back(".");
  settings = std::move(loaded);
  return true;
}

bool saveEditorSettings(const std::filesystem::path& content_root,
                        const EditorSettings& settings,
                        std::string* diagnostic) {
  Json roots = Json::array();
  for (const auto& root : settings.asset_roots) roots.push_back(root.generic_string());
  const char* bottom_panel_tab = "assets";
  switch (settings.bottom_panel_tab) {
    case BottomPanelTab::Assets: bottom_panel_tab = "assets"; break;
    case BottomPanelTab::Console: bottom_panel_tab = "console"; break;
    case BottomPanelTab::Lighting: bottom_panel_tab = "lighting"; break;
    case BottomPanelTab::Navigation: bottom_panel_tab = "navigation"; break;
  }
  return atomicWriteJson(settingsPath(content_root),
                         Json{{"version", 1},
                              {"asset_roots", std::move(roots)},
                              {"camera_move_speed", settings.camera_move_speed},
                              {"grid_size", settings.grid_size},
                              {"snap_enabled", settings.snap_enabled},
                              {"markers_visible", settings.markers_visible},
                              {"layout",
                               {{"hierarchy_width", settings.panel_layout.hierarchy_width},
                                {"inspector_width", settings.panel_layout.inspector_width},
                                {"assets_height", settings.panel_layout.assets_height}}},
                              {"asset_filter", settings.asset_filter},
                              {"hierarchy_filter", settings.hierarchy_filter},
                              {"inspector_filter", settings.inspector_filter},
                              {"selected_bake_id", settings.selected_bake_id},
                              {"asset_type_filter", settings.asset_type_filter},
                              {"console_min_level", settings.console_min_level},
                              {"terrain_inspector_tab", settings.terrain_inspector_tab},
                              {"terrain_material_layer", settings.terrain_material_layer},
                              {"active_foliage_layer_id",
                               settings.active_foliage_layer_id},
                              {"component_foldouts",
                               settings.component_foldouts},
                              {"bottom_panel_tab", bottom_panel_tab},
                              {"viewport_render_mode",
                               viewportRenderModeName(settings.viewport_render_mode)}},
                         diagnostic);
}

std::string makeStableId(std::string_view prefix) {
  static std::mutex generator_mutex;
  static std::mt19937_64 generator([] {
    std::random_device random;
    const uint64_t time = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return std::mt19937_64(random() ^ time);
  }());
  const std::lock_guard lock(generator_mutex);
  std::array<uint64_t, 2> words{generator(), generator()};
  std::ostringstream stream;
  stream << prefix << '_' << std::hex << std::setfill('0')
         << std::setw(16) << words[0] << std::setw(16) << words[1];
  return stream.str();
}

void DocumentHistory::setLimits(size_t max_commands, size_t max_bytes) {
  max_commands_ = std::max<size_t>(max_commands, 1u);
  max_bytes_ = std::max<size_t>(max_bytes, 1u);
  enforceLimits();
}

void DocumentHistory::clear() {
  entries_.clear();
  cursor_ = 0u;
  saved_cursor_ = 0u;
  saved_state_reachable_ = true;
  total_bytes_ = 0u;
}

void DocumentHistory::push(std::string label,
                           scenes::SceneDocument before,
                           scenes::SceneDocument after) {
  if (saved_state_reachable_ && saved_cursor_ > cursor_) {
    saved_state_reachable_ = false;
  }
  while (entries_.size() > cursor_) {
    total_bytes_ -= entries_.back().estimated_bytes;
    entries_.pop_back();
  }
  const size_t bytes = estimateDocumentBytes(before) + estimateDocumentBytes(after);
  entries_.push_back(Entry{std::move(label), std::move(before), std::move(after), bytes});
  total_bytes_ += bytes;
  cursor_ = entries_.size();
  enforceLimits();
}

bool DocumentHistory::undo(scenes::SceneDocument& document) {
  if (!canUndo()) return false;
  --cursor_;
  document = entries_[cursor_].before;
  return true;
}

bool DocumentHistory::redo(scenes::SceneDocument& document) {
  if (!canRedo()) return false;
  document = entries_[cursor_].after;
  ++cursor_;
  return true;
}

std::string_view DocumentHistory::undoLabel() const {
  return canUndo() ? std::string_view(entries_[cursor_ - 1u].label) : std::string_view{};
}

std::string_view DocumentHistory::redoLabel() const {
  return canRedo() ? std::string_view(entries_[cursor_].label) : std::string_view{};
}

void DocumentHistory::markSaved() {
  saved_cursor_ = cursor_;
  saved_state_reachable_ = true;
}

void DocumentHistory::enforceLimits() {
  while (!entries_.empty() &&
         (entries_.size() > max_commands_ || total_bytes_ > max_bytes_)) {
    total_bytes_ -= entries_.front().estimated_bytes;
    entries_.erase(entries_.begin());
    if (cursor_ > 0u) --cursor_;
    if (saved_state_reachable_) {
      if (saved_cursor_ > 0u) {
        --saved_cursor_;
      } else {
        saved_state_reachable_ = false;
      }
    }
  }
}

std::filesystem::path recoveryPath(const std::filesystem::path& content_root,
                                   const std::filesystem::path& scene_path) {
  return content_root / ".karma" / "recovery" /
         (recoveryKey(scene_path) + ".scene-recovery.json");
}

bool writeRecovery(const std::filesystem::path& content_root,
                   const std::filesystem::path& scene_path,
                   const Json& scene_json,
                   std::string* diagnostic) {
  if (!scene_json.is_object()) {
    return fail(diagnostic, "recovery scene must be a JSON object");
  }
  return atomicWriteJson(recoveryPath(content_root, scene_path),
                         Json{{"version", 1},
                              {"source_scene", weakCanonical(scene_path).generic_string()},
                              {"scene", scene_json}},
                         diagnostic);
}

std::optional<RecoveryRecord> loadRecovery(const std::filesystem::path& content_root,
                                           const std::filesystem::path& scene_path,
                                           std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  std::filesystem::path path = recoveryPath(content_root, scene_path);
  std::error_code ec;
  bool exists = std::filesystem::exists(path, ec);
  if (ec) {
    fail(diagnostic, "failed to inspect recovery file: " + ec.message());
    return std::nullopt;
  }
  if (!exists) {
    const std::filesystem::path legacy =
        legacyRecoveryPath(content_root, scene_path);
    if (legacy != path) {
      exists = std::filesystem::exists(legacy, ec);
      if (ec) {
        fail(diagnostic, "failed to inspect legacy recovery file: " +
                             ec.message());
        return std::nullopt;
      }
      if (exists) path = legacy;
    }
  }
  if (!exists) return std::nullopt;
  Json json;
  std::string error;
  if (!readJson(path, json, error) || !json.is_object()) {
    fail(diagnostic, error.empty() ? "recovery file is malformed" : error);
    return std::nullopt;
  }
  const auto version = json.find("version");
  if (version == json.end() || !isVersionOne(*version)) {
    fail(diagnostic, "recovery version must be the integer 1");
    return std::nullopt;
  }
  const auto source_scene = json.find("source_scene");
  if (source_scene == json.end() || !source_scene->is_string()) {
    fail(diagnostic, "recovery source_scene must be a string");
    return std::nullopt;
  }
  const auto scene = json.find("scene");
  if (scene == json.end() || !scene->is_object()) {
    fail(diagnostic, "recovery scene must be an object");
    return std::nullopt;
  }
  RecoveryRecord record{};
  record.source_scene = source_scene->get<std::string>();
  if (record.source_scene.empty() ||
      weakCanonical(record.source_scene) != weakCanonical(scene_path)) {
    fail(diagnostic,
         "recovery source_scene does not match the requested scene");
    return std::nullopt;
  }
  record.written = std::filesystem::last_write_time(path, ec);
  if (ec) {
    fail(diagnostic, "failed to query recovery timestamp: " + ec.message());
    return std::nullopt;
  }
  record.scene_json = std::move(*scene);
  return record;
}

bool discardRecovery(const std::filesystem::path& content_root,
                     const std::filesystem::path& scene_path,
                     std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  const std::array<std::filesystem::path, 2u> paths{
      recoveryPath(content_root, scene_path),
      legacyRecoveryPath(content_root, scene_path),
  };
  for (size_t index = 0u; index < paths.size(); ++index) {
    const std::filesystem::path& path = paths[index];
    if (index > 0u && path == paths[0]) continue;
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
      return fail(diagnostic, "failed to inspect recovery file " +
                                  path.string() + ": " + ec.message());
    }
    if (!exists) continue;
    if (!std::filesystem::remove(path, ec) || ec) {
      return fail(diagnostic, "failed to remove recovery file " +
                                  path.string() + ": " + ec.message());
    }
  }
  return true;
}

}  // namespace karma::tools::scene_editor
