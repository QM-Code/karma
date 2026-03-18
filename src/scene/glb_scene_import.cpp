#include "karma/scene/glb_scene_import.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string_view>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "karma/components/mesh.h"
#include "karma/components/tag.h"
#include "karma/components/transform.h"

namespace karma::scene {

namespace {
math::Vec3 toVec3(const aiVector3D& v) {
  return {v.x, v.y, v.z};
}

math::Quat toQuat(const aiQuaternion& q) {
  return {q.x, q.y, q.z, q.w};
}

std::string safeName(std::string_view base, std::string_view fallback) {
  return base.empty() ? std::string(fallback) : std::string(base);
}

renderer::MeshData buildMeshData(const aiMesh& mesh) {
  renderer::MeshData out{};
  out.vertices.reserve(mesh.mNumVertices);
  out.normals.reserve(mesh.mNumVertices);
  out.uvs.reserve(mesh.mNumVertices);
  out.tangents.reserve(mesh.mNumVertices);

  for (unsigned int v = 0; v < mesh.mNumVertices; ++v) {
    const auto& pos = mesh.mVertices[v];
    out.vertices.emplace_back(pos.x, pos.y, pos.z);

    if (mesh.HasNormals()) {
      const auto& n = mesh.mNormals[v];
      out.normals.emplace_back(n.x, n.y, n.z);
    } else {
      out.normals.emplace_back(0.0f, 1.0f, 0.0f);
    }

    if (mesh.HasTextureCoords(0)) {
      const auto& uv = mesh.mTextureCoords[0][v];
      out.uvs.emplace_back(uv.x, uv.y);
    } else {
      out.uvs.emplace_back(0.0f, 0.0f);
    }

    if (mesh.HasTangentsAndBitangents()) {
      const auto& t = mesh.mTangents[v];
      const auto& b = mesh.mBitangents[v];
      const auto& n = mesh.mNormals[v];
      const glm::vec3 tangent{t.x, t.y, t.z};
      const glm::vec3 bitangent{b.x, b.y, b.z};
      const glm::vec3 normal{n.x, n.y, n.z};
      const float sign =
          glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
      out.tangents.emplace_back(tangent.x, tangent.y, tangent.z, sign);
    } else {
      out.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    }
  }

  for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
    const aiFace& face = mesh.mFaces[f];
    if (face.mNumIndices != 3) {
      continue;
    }
    out.indices.push_back(face.mIndices[0]);
    out.indices.push_back(face.mIndices[1]);
    out.indices.push_back(face.mIndices[2]);
  }

  return out;
}

renderer::MaterialDesc buildMaterialDesc(const aiScene& scene, const aiMesh& mesh) {
  renderer::MaterialDesc desc{};
  desc.base_color = {1.0f, 1.0f, 1.0f, 1.0f};

  if (mesh.mMaterialIndex >= scene.mNumMaterials || scene.mMaterials[mesh.mMaterialIndex] == nullptr) {
    return desc;
  }

  const aiMaterial* material = scene.mMaterials[mesh.mMaterialIndex];
  aiColor4D base_color(1.0f, 1.0f, 1.0f, 1.0f);
  if (material->Get(AI_MATKEY_BASE_COLOR, base_color) == AI_SUCCESS) {
    desc.base_color = {base_color.r, base_color.g, base_color.b, base_color.a};
  } else {
    aiColor3D diffuse(1.0f, 1.0f, 1.0f);
    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
      desc.base_color = {diffuse.r, diffuse.g, diffuse.b, 1.0f};
    }
  }

  float opacity = desc.base_color.a;
  if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
    desc.base_color.a = opacity;
  }

  int two_sided = 0;
  if (material->Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) {
    desc.double_sided = two_sided != 0;
  }

  desc.transparent = desc.base_color.a < 0.999f;
  return desc;
}

float estimateLightRange(const aiLight& light) {
  if (light.mAttenuationLinear > 1e-5f) {
    return std::max(1.0f, 1.0f / light.mAttenuationLinear);
  }
  if (light.mAttenuationQuadratic > 1e-5f &&
      (light.mAttenuationConstant > 1e-5f || light.mAttenuationLinear > 1e-5f ||
       std::abs(light.mAttenuationQuadratic - 1.0f) > 1e-5f)) {
    return std::max(1.0f, std::sqrt(1.0f / light.mAttenuationQuadratic));
  }
  return 10.0f;
}

components::LightComponent buildLightComponent(const aiLight& light) {
  constexpr float kRadiansToDegrees = 57.29577951308232f;
  constexpr float kDirectionalIntensityScale = 1.0f / 700.0f;
  constexpr float kLocalLightIntensityScale = 1.0f / 50.0f;
  constexpr float kLocalLightCutoffIntensity = 0.05f;
  components::LightComponent out{};
  float intensity_scale = kLocalLightIntensityScale;

  switch (light.mType) {
    case aiLightSource_DIRECTIONAL:
      out.type = components::LightComponent::Type::Directional;
      intensity_scale = kDirectionalIntensityScale;
      break;
    case aiLightSource_SPOT:
      out.type = components::LightComponent::Type::Spot;
      out.casts_shadows = true;
      out.inner_cone_degrees = light.mAngleInnerCone * kRadiansToDegrees;
      out.outer_cone_degrees = light.mAngleOuterCone * kRadiansToDegrees;
      break;
    case aiLightSource_POINT:
    default:
      out.type = components::LightComponent::Type::Point;
      out.casts_shadows = true;
      break;
  }

  math::Color diffuse{
      light.mColorDiffuse.r,
      light.mColorDiffuse.g,
      light.mColorDiffuse.b,
      1.0f};
  const float max_channel =
      std::max(diffuse.r, std::max(diffuse.g, std::max(diffuse.b, 0.0f)));
  if (max_channel > 1e-5f) {
    out.color = {diffuse.r / max_channel, diffuse.g / max_channel, diffuse.b / max_channel, 1.0f};
    out.intensity = max_channel * intensity_scale;
  } else {
    out.color = {1.0f, 1.0f, 1.0f, 1.0f};
    out.intensity = 1.0f;
  }

  if (out.type == components::LightComponent::Type::Point ||
      out.type == components::LightComponent::Type::Spot) {
    const bool gltf_default_quadratic =
        light.mAttenuationQuadratic > 1e-5f &&
        light.mAttenuationConstant <= 1e-5f &&
        light.mAttenuationLinear <= 1e-5f &&
        std::abs(light.mAttenuationQuadratic - 1.0f) <= 1e-5f;
    if (gltf_default_quadratic) {
      out.range = std::clamp(std::sqrt(std::max(out.intensity, 0.0f) / kLocalLightCutoffIntensity),
                             4.0f,
                             40.0f);
    } else {
      out.range = estimateLightRange(light);
    }
  }

  return out;
}

void decomposeTransform(const aiMatrix4x4& transform,
                        math::Vec3& position,
                        math::Quat& rotation,
                        math::Vec3& scale) {
  aiVector3D ai_scale;
  aiQuaternion ai_rotation;
  aiVector3D ai_position;
  transform.Decompose(ai_scale, ai_rotation, ai_position);
  position = toVec3(ai_position);
  rotation = toQuat(ai_rotation);
  scale = toVec3(ai_scale);
}

uint32_t loadNodePrefab(const aiScene& scene,
                        const aiNode& node,
                        const aiMatrix4x4& parent_world,
                        const std::unordered_map<std::string, const aiLight*>& lights_by_name,
                        const GlbSceneLoadOptions& options,
                        GlbScenePrefab& prefab) {
  const aiMatrix4x4 world_transform = parent_world * node.mTransformation;

  GlbScenePrefabNode prefab_node{};
  prefab_node.name = node.mName.C_Str();
  decomposeTransform(node.mTransformation,
                     prefab_node.local_position,
                     prefab_node.local_rotation,
                     prefab_node.local_scale);
  decomposeTransform(world_transform,
                     prefab_node.world_position,
                     prefab_node.world_rotation,
                     prefab_node.world_scale);

  if (options.import_meshes) {
    prefab_node.primitives.reserve(node.mNumMeshes);
    for (unsigned int mesh_index = 0; mesh_index < node.mNumMeshes; ++mesh_index) {
      const unsigned int scene_mesh_index = node.mMeshes[mesh_index];
      if (scene_mesh_index >= scene.mNumMeshes || scene.mMeshes[scene_mesh_index] == nullptr) {
        continue;
      }
      const aiMesh& mesh = *scene.mMeshes[scene_mesh_index];
      GlbScenePrefabPrimitive primitive{};
      primitive.name = safeName(mesh.mName.C_Str(), "Primitive");
      primitive.mesh = buildMeshData(mesh);
      primitive.material = buildMaterialDesc(scene, mesh);
      primitive.source_material_index =
          mesh.mMaterialIndex < scene.mNumMaterials ? mesh.mMaterialIndex : kInvalidGlbSceneMaterial;
      prefab_node.primitives.push_back(std::move(primitive));
    }
  }

  if (options.import_lights) {
    const auto light_it = lights_by_name.find(node.mName.C_Str());
    if (light_it != lights_by_name.end() && light_it->second != nullptr) {
      prefab_node.has_light = true;
      prefab_node.light = buildLightComponent(*light_it->second);
    }
  }

  const uint32_t node_index = static_cast<uint32_t>(prefab.nodes.size());
  prefab.nodes.push_back(std::move(prefab_node));

  prefab.nodes[node_index].children.reserve(node.mNumChildren);
  for (unsigned int child_index = 0; child_index < node.mNumChildren; ++child_index) {
    const aiNode* child = node.mChildren[child_index];
    if (child == nullptr) {
      continue;
    }
    const uint32_t imported_child =
        loadNodePrefab(scene, *child, world_transform, lights_by_name, options, prefab);
    if (imported_child != kInvalidGlbSceneNode) {
      prefab.nodes[node_index].children.push_back(imported_child);
    }
  }

  return node_index;
}

std::string nodeDisplayName(const GlbScenePrefabNode& node, uint32_t index) {
  if (!node.name.empty()) {
    return node.name;
  }
  return "GLB Node " + std::to_string(index);
}

std::string primitiveDisplayName(const GlbScenePrefabNode& node,
                                 uint32_t node_index,
                                 const GlbScenePrefabPrimitive& primitive,
                                 size_t primitive_index) {
  if (!primitive.name.empty()) {
    return primitive.name;
  }
  return nodeDisplayName(node, node_index) + " Primitive " + std::to_string(primitive_index);
}
}  // namespace

GlbScenePrefab loadGlbScenePrefab(const std::filesystem::path& path,
                                  const GlbSceneLoadOptions& options) {
  GlbScenePrefab prefab{};
  prefab.source_path = path;

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(path.string(),
                                           aiProcess_Triangulate |
                                           aiProcess_GenNormals |
                                           aiProcess_CalcTangentSpace |
                                           aiProcess_JoinIdenticalVertices);
  if (scene == nullptr || scene->mRootNode == nullptr) {
    return prefab;
  }

  std::unordered_map<std::string, const aiLight*> lights_by_name;
  lights_by_name.reserve(scene->mNumLights);
  for (unsigned int i = 0; i < scene->mNumLights; ++i) {
    const aiLight* light = scene->mLights[i];
    if (light == nullptr) {
      continue;
    }
    lights_by_name[light->mName.C_Str()] = light;
  }

  prefab.root_node = loadNodePrefab(*scene,
                                    *scene->mRootNode,
                                    aiMatrix4x4{},
                                    lights_by_name,
                                    options,
                                    prefab);
  return prefab;
}

GlbSceneImportResult instantiateGlbScenePrefab(
    ecs::World& world,
    scene::Scene& scene,
    renderer::GraphicsDevice& device,
    const GlbScenePrefab& prefab,
    const GlbSceneInstantiateOptions& options) {
  GlbSceneImportResult result{};
  if (!prefab.valid()) {
    return result;
  }

  std::function<std::pair<ecs::Entity, scene::NodeId>(uint32_t, scene::NodeId)> instantiate_node;
  instantiate_node = [&](uint32_t prefab_node_index,
                         scene::NodeId parent_node) -> std::pair<ecs::Entity, scene::NodeId> {
    const auto& prefab_node = prefab.nodes[prefab_node_index];
    const ecs::Entity entity = world.createEntity();
    world.setName(entity, nodeDisplayName(prefab_node, prefab_node_index));
    world.add(entity, components::TransformComponent{
                           prefab_node.world_position,
                           prefab_node.world_rotation,
                           prefab_node.world_scale});
    if (prefab_node.has_light) {
      world.add(entity, prefab_node.light);
    }

    const scene::NodeId node_id = scene.createNode(entity);
    if (parent_node != scene::Node::kInvalidId) {
      scene.reparent(node_id, parent_node);
    }
    result.entities.push_back(entity);

    for (size_t primitive_index = 0; primitive_index < prefab_node.primitives.size(); ++primitive_index) {
      const auto& primitive = prefab_node.primitives[primitive_index];
      const ecs::Entity primitive_entity = world.createEntity();
      world.setName(primitive_entity,
                    primitiveDisplayName(prefab_node, prefab_node_index, primitive, primitive_index));
      world.add(primitive_entity, components::TransformComponent{
                                     prefab_node.world_position,
                                     prefab_node.world_rotation,
                                     prefab_node.world_scale});

      const renderer::MeshId mesh_id = device.createMesh(primitive.mesh);
      renderer::MaterialId material_id = renderer::kInvalidMaterial;
      if (primitive.source_material_index != kInvalidGlbSceneMaterial &&
          !prefab.source_path.empty()) {
        material_id = device.createMaterialFromAsset(prefab.source_path, primitive.source_material_index);
      }
      if (material_id == renderer::kInvalidMaterial) {
        material_id = device.createMaterial(primitive.material);
      }
      world.add(primitive_entity, components::MeshComponent{
                                     .mesh_id = mesh_id,
                                     .material_id = material_id,
                                     .owns_mesh_id = mesh_id != renderer::kInvalidMesh,
                                     .owns_material_id = material_id != renderer::kInvalidMaterial,
                                     .visible = true});

      const scene::NodeId primitive_node = scene.createNode(primitive_entity);
      scene.reparent(primitive_node, node_id);
      result.entities.push_back(primitive_entity);
    }

    for (const uint32_t child_index : prefab_node.children) {
      instantiate_node(child_index, node_id);
    }

    return {entity, node_id};
  };

  if (options.create_synthetic_root) {
    const ecs::Entity root_entity = world.createEntity();
    const std::string root_name =
        prefab.source_path.stem().empty() ? std::string("Imported GLB") : prefab.source_path.stem().string();
    world.setName(root_entity, root_name);
    world.add(root_entity, components::TransformComponent{});
    const scene::NodeId root_node = scene.createNode(root_entity);
    result.entities.push_back(root_entity);
    instantiate_node(prefab.root_node, root_node);
    result.root_entity = root_entity;
    result.root_node = root_node;
    return result;
  }

  const auto [root_entity, root_node] = instantiate_node(prefab.root_node, scene::Node::kInvalidId);
  result.root_entity = root_entity;
  result.root_node = root_node;
  return result;
}

GlbSceneImportResult importGlbScene(ecs::World& world,
                                    scene::Scene& scene,
                                    renderer::GraphicsDevice& device,
                                    const std::filesystem::path& path,
                                    const GlbSceneImportOptions& options) {
  const GlbScenePrefab prefab = loadGlbScenePrefab(path, options.load);
  return instantiateGlbScenePrefab(world, scene, device, prefab, options.instantiate);
}

}  // namespace karma::scene
