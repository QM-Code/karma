#include "karma/content/importers/glb_scene_import.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string_view>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "karma/world/components/animation_player.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/tag.h"
#include "karma/world/components/transform.h"
#include "karma/world/scene/transform_hierarchy.h"

namespace karma::scene {

namespace {
math::Vec3 toVec3(const aiVector3D& v) {
  return {v.x, v.y, v.z};
}

math::Quat toQuat(const aiQuaternion& q) {
  return {q.x, q.y, q.z, q.w};
}

glm::mat4 toGlm(const aiMatrix4x4& m) {
  return glm::mat4{
      m.a1, m.b1, m.c1, m.d1,
      m.a2, m.b2, m.c2, m.d2,
      m.a3, m.b3, m.c3, m.d3,
      m.a4, m.b4, m.c4, m.d4,
  };
}

std::string safeName(std::string_view base, std::string_view fallback) {
  return base.empty() ? std::string(fallback) : std::string(base);
}

void addInfluence(components::VertexSkinInfluence& influence, uint32_t joint, float weight) {
  if (weight <= 0.0f) {
    return;
  }

  for (int i = 0; i < 4; ++i) {
    if (influence.weights[i] <= 0.0f) {
      influence.joints[i] = joint;
      influence.weights[i] = weight;
      return;
    }
  }

  int weakest = 0;
  for (int i = 1; i < 4; ++i) {
    if (influence.weights[i] < influence.weights[weakest]) {
      weakest = i;
    }
  }
  if (weight > influence.weights[weakest]) {
    influence.joints[weakest] = joint;
    influence.weights[weakest] = weight;
  }
}

void normalizeInfluence(components::VertexSkinInfluence& influence) {
  const float sum =
      influence.weights.x + influence.weights.y + influence.weights.z + influence.weights.w;
  if (sum <= 0.0f) {
    return;
  }
  influence.weights /= sum;
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
                        GlbScenePrefab& prefab,
                        std::unordered_map<std::string, uint32_t>& node_indices_by_name) {
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
      primitive.source_mesh_index = scene_mesh_index;
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
  node_indices_by_name.try_emplace(node.mName.C_Str(), node_index);

  prefab.nodes[node_index].children.reserve(node.mNumChildren);
  for (unsigned int child_index = 0; child_index < node.mNumChildren; ++child_index) {
    const aiNode* child = node.mChildren[child_index];
    if (child == nullptr) {
      continue;
    }
    const uint32_t imported_child =
        loadNodePrefab(scene,
                       *child,
                       world_transform,
                       lights_by_name,
                       options,
                       prefab,
                       node_indices_by_name);
    if (imported_child != kInvalidGlbSceneNode) {
      prefab.nodes[node_index].children.push_back(imported_child);
    }
  }

  return node_index;
}

void populatePrimitiveSkinning(const aiScene& scene,
                               const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
                               GlbScenePrefab& prefab) {
  for (GlbScenePrefabNode& node : prefab.nodes) {
    for (GlbScenePrefabPrimitive& primitive : node.primitives) {
      if (primitive.source_mesh_index >= scene.mNumMeshes ||
          scene.mMeshes[primitive.source_mesh_index] == nullptr) {
        continue;
      }
      const aiMesh& mesh = *scene.mMeshes[primitive.source_mesh_index];
      if (!mesh.HasBones() || primitive.mesh.vertices.empty()) {
        continue;
      }

      primitive.vertex_influences.assign(primitive.mesh.vertices.size(),
                                         components::VertexSkinInfluence{});
      primitive.joint_node_indices.clear();
      primitive.inverse_bind_matrices.clear();
      primitive.joint_node_indices.reserve(mesh.mNumBones);
      primitive.inverse_bind_matrices.reserve(mesh.mNumBones);

      for (unsigned int bone_index = 0; bone_index < mesh.mNumBones; ++bone_index) {
        const aiBone* bone = mesh.mBones[bone_index];
        if (bone == nullptr) {
          continue;
        }
        const auto node_it = node_indices_by_name.find(bone->mName.C_Str());
        if (node_it == node_indices_by_name.end()) {
          continue;
        }

        const uint32_t joint_index = static_cast<uint32_t>(primitive.joint_node_indices.size());
        primitive.joint_node_indices.push_back(node_it->second);
        primitive.inverse_bind_matrices.push_back(toGlm(bone->mOffsetMatrix));

        for (unsigned int weight_index = 0; weight_index < bone->mNumWeights; ++weight_index) {
          const aiVertexWeight& weight = bone->mWeights[weight_index];
          if (weight.mVertexId >= primitive.vertex_influences.size()) {
            continue;
          }
          addInfluence(primitive.vertex_influences[weight.mVertexId],
                       joint_index,
                       weight.mWeight);
        }
      }

      for (auto& influence : primitive.vertex_influences) {
        normalizeInfluence(influence);
      }
      if (primitive.joint_node_indices.empty()) {
        primitive.vertex_influences.clear();
        primitive.inverse_bind_matrices.clear();
      }
    }
  }
}

std::vector<animation::AnimationClip> loadAnimationClips(
    const aiScene& scene,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name) {
  std::vector<animation::AnimationClip> clips;
  clips.reserve(scene.mNumAnimations);

  for (unsigned int animation_index = 0; animation_index < scene.mNumAnimations; ++animation_index) {
    const aiAnimation* source = scene.mAnimations[animation_index];
    if (source == nullptr) {
      continue;
    }

    const double ticks_per_second =
        source->mTicksPerSecond > 0.0 ? source->mTicksPerSecond : 1.0;
    animation::AnimationClip clip{};
    clip.name = safeName(source->mName.C_Str(), "Animation " + std::to_string(animation_index));
    clip.ticks_per_second = static_cast<float>(ticks_per_second);
    clip.duration_seconds = static_cast<float>(source->mDuration / ticks_per_second);
    clip.channels.reserve(source->mNumChannels);

    for (unsigned int channel_index = 0; channel_index < source->mNumChannels; ++channel_index) {
      const aiNodeAnim* source_channel = source->mChannels[channel_index];
      if (source_channel == nullptr) {
        continue;
      }
      const auto node_it = node_indices_by_name.find(source_channel->mNodeName.C_Str());
      if (node_it == node_indices_by_name.end()) {
        continue;
      }

      animation::AnimationChannel channel{};
      channel.target_node_index = node_it->second;
      channel.position_keys.reserve(source_channel->mNumPositionKeys);
      channel.rotation_keys.reserve(source_channel->mNumRotationKeys);
      channel.scale_keys.reserve(source_channel->mNumScalingKeys);

      for (unsigned int key_index = 0; key_index < source_channel->mNumPositionKeys; ++key_index) {
        const aiVectorKey& key = source_channel->mPositionKeys[key_index];
        channel.position_keys.push_back(animation::Vec3Keyframe{
            .time_seconds = static_cast<float>(key.mTime / ticks_per_second),
            .value = toVec3(key.mValue),
        });
      }
      for (unsigned int key_index = 0; key_index < source_channel->mNumRotationKeys; ++key_index) {
        const aiQuatKey& key = source_channel->mRotationKeys[key_index];
        channel.rotation_keys.push_back(animation::QuatKeyframe{
            .time_seconds = static_cast<float>(key.mTime / ticks_per_second),
            .value = toQuat(key.mValue),
        });
      }
      for (unsigned int key_index = 0; key_index < source_channel->mNumScalingKeys; ++key_index) {
        const aiVectorKey& key = source_channel->mScalingKeys[key_index];
        channel.scale_keys.push_back(animation::Vec3Keyframe{
            .time_seconds = static_cast<float>(key.mTime / ticks_per_second),
            .value = toVec3(key.mValue),
        });
      }

      clip.channels.push_back(std::move(channel));
    }

    if (!clip.channels.empty()) {
      clips.push_back(std::move(clip));
    }
  }

  return clips;
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

  std::unordered_map<std::string, uint32_t> node_indices_by_name;
  node_indices_by_name.reserve(128);
  prefab.root_node = loadNodePrefab(*scene,
                                    *scene->mRootNode,
                                    aiMatrix4x4{},
                                    lights_by_name,
                                    options,
                                    prefab,
                                    node_indices_by_name);
  populatePrimitiveSkinning(*scene, node_indices_by_name, prefab);
  prefab.animations = loadAnimationClips(*scene, node_indices_by_name);
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

  result.node_entities_by_index.resize(prefab.nodes.size());

  struct PendingSkin {
    ecs::Entity entity{};
    const GlbScenePrefabPrimitive* primitive = nullptr;
  };
  std::vector<PendingSkin> pending_skins;

  auto attach_pending_skins = [&]() {
    for (const PendingSkin& pending : pending_skins) {
      if (!world.isAlive(pending.entity) || pending.primitive == nullptr ||
          !pending.primitive->skinned()) {
        continue;
      }
      std::vector<ecs::Entity> joint_entities;
      joint_entities.reserve(pending.primitive->joint_node_indices.size());
      for (const uint32_t joint_node_index : pending.primitive->joint_node_indices) {
        if (joint_node_index < result.node_entities_by_index.size()) {
          joint_entities.push_back(result.node_entities_by_index[joint_node_index]);
        } else {
          joint_entities.push_back({});
        }
      }
      world.add(pending.entity, components::SkinnedMeshComponent{
                                    .bind_mesh = pending.primitive->mesh,
                                    .skinned_mesh = pending.primitive->mesh,
                                    .vertex_influences = pending.primitive->vertex_influences,
                                    .joint_entities = std::move(joint_entities),
                                    .inverse_bind_matrices = pending.primitive->inverse_bind_matrices,
                                    .enabled = true});
    }
  };

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
    world.add(entity, components::LocalTransformComponent{
                          prefab_node.local_position,
                          prefab_node.local_rotation,
                          prefab_node.local_scale});
    if (prefab_node.has_light) {
      world.add(entity, prefab_node.light);
    }

    const scene::NodeId node_id = scene.createNode(entity);
    if (parent_node != scene::Node::kInvalidId) {
      scene.reparent(node_id, parent_node);
    }
    result.entities.push_back(entity);
    if (prefab_node_index < result.node_entities_by_index.size()) {
      result.node_entities_by_index[prefab_node_index] = entity;
    }

    for (size_t primitive_index = 0; primitive_index < prefab_node.primitives.size(); ++primitive_index) {
      const auto& primitive = prefab_node.primitives[primitive_index];
      const ecs::Entity primitive_entity = world.createEntity();
      world.setName(primitive_entity,
                    primitiveDisplayName(prefab_node, prefab_node_index, primitive, primitive_index));
      world.add(primitive_entity, components::TransformComponent{
                                     prefab_node.world_position,
                                     prefab_node.world_rotation,
                                     prefab_node.world_scale});
      world.add(primitive_entity, components::LocalTransformComponent{});

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
      if (primitive.skinned()) {
        pending_skins.push_back(PendingSkin{.entity = primitive_entity, .primitive = &primitive});
      }

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
    world.add(root_entity, components::LocalTransformComponent{});
    const scene::NodeId root_node = scene.createNode(root_entity);
    result.entities.push_back(root_entity);
    instantiate_node(prefab.root_node, root_node);
    result.root_entity = root_entity;
    result.root_node = root_node;
    attach_pending_skins();
    if (!prefab.animations.empty()) {
      world.add(root_entity, components::AnimationPlayerComponent{
                                 .clips = prefab.animations,
                                 .node_entities_by_index = result.node_entities_by_index,
                                 .current_clip_index = 0,
                                 .time_seconds = 0.0f,
                                 .speed = 1.0f,
                                 .loop = true,
                                 .playing = options.autoplay_animations});
    }
    updateWorldTransforms(world, scene);
    return result;
  }

  const auto [root_entity, root_node] = instantiate_node(prefab.root_node, scene::Node::kInvalidId);
  result.root_entity = root_entity;
  result.root_node = root_node;
  attach_pending_skins();
  if (!prefab.animations.empty()) {
    world.add(root_entity, components::AnimationPlayerComponent{
                               .clips = prefab.animations,
                               .node_entities_by_index = result.node_entities_by_index,
                               .current_clip_index = 0,
                               .time_seconds = 0.0f,
                               .speed = 1.0f,
                               .loop = true,
                               .playing = options.autoplay_animations});
  }
  updateWorldTransforms(world, scene);
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
