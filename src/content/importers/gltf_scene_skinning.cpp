#include "gltf_scene_skinning.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assimp/scene.h>

namespace karma::world {

namespace {

glm::mat4 toGlm(const aiMatrix4x4& m) {
  return glm::mat4{
      m.a1, m.b1, m.c1, m.d1,
      m.a2, m.b2, m.c2, m.d2,
      m.a3, m.b3, m.c3, m.d3,
      m.a4, m.b4, m.c4, m.d4,
  };
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

void setInfluenceMeshAttributes(GltfScenePrefabPrimitive& primitive) {
  primitive.mesh.joint_indices.clear();
  primitive.mesh.joint_weights.clear();
  primitive.mesh.joint_indices.reserve(primitive.vertex_influences.size());
  primitive.mesh.joint_weights.reserve(primitive.vertex_influences.size());
  for (const auto& influence : primitive.vertex_influences) {
    primitive.mesh.joint_indices.push_back(influence.joints);
    primitive.mesh.joint_weights.push_back(influence.weights);
  }
}

bool populatePrimitiveInfluencesFromGltf(const GltfDocument& doc,
                                         const Json& source_primitive,
                                         const world::Skin& skin,
                                         GltfScenePrefabPrimitive& primitive) {
  if (!source_primitive.contains("attributes") ||
      !source_primitive["attributes"].is_object()) {
    return false;
  }
  const Json& attributes = source_primitive["attributes"];
  if (!attributes.contains("JOINTS_0") || !attributes.contains("WEIGHTS_0")) {
    return false;
  }

  std::vector<glm::uvec4> joints;
  std::vector<glm::vec4> weights;
  if (!readVec4JointAccessor(doc, attributes["JOINTS_0"].get<uint32_t>(), joints) ||
      !readVec4WeightAccessor(doc, attributes["WEIGHTS_0"].get<uint32_t>(), weights) ||
      joints.size() != weights.size() ||
      joints.size() != primitive.mesh.vertices.size()) {
    return false;
  }

  primitive.joint_node_indices = skin.joint_node_indices;
  primitive.inverse_bind_matrices = skin.inverse_bind_matrices;
  primitive.vertex_influences.clear();
  primitive.vertex_influences.reserve(joints.size());
  for (size_t i = 0; i < joints.size(); ++i) {
    components::VertexSkinInfluence influence{};
    for (int slot = 0; slot < 4; ++slot) {
      const uint32_t joint_index = joints[i][slot];
      if (joint_index >= skin.joint_node_indices.size()) {
        continue;
      }
      influence.joints[slot] = joint_index;
      influence.weights[slot] = weights[i][slot];
    }
    normalizeInfluence(influence);
    primitive.vertex_influences.push_back(influence);
  }
  setInfluenceMeshAttributes(primitive);
  return true;
}

}  // namespace

world::Skeleton makeNodeHierarchySkeleton(const GltfScenePrefab& prefab,
                                          std::string_view name) {
  world::Skeleton skeleton{};
  skeleton.name = name.empty() ? std::string("Node Hierarchy") : std::string(name);
  skeleton.joints.reserve(prefab.nodes.size());
  std::vector<uint32_t> parent_node_indices(prefab.nodes.size(),
                                            world::kInvalidAnimationIndex);
  for (uint32_t node_index = 0u; node_index < prefab.nodes.size(); ++node_index) {
    for (const uint32_t child_index : prefab.nodes[node_index].children) {
      if (child_index < parent_node_indices.size()) {
        parent_node_indices[child_index] = node_index;
      }
    }
  }
  for (uint32_t node_index = 0u; node_index < prefab.nodes.size(); ++node_index) {
    const GltfScenePrefabNode& node = prefab.nodes[node_index];
    world::Joint joint{};
    joint.name = node.name;
    joint.node_index = node_index;
    joint.parent_joint_index = parent_node_indices[node_index];
    joint.rest_local_position = node.local_position;
    joint.rest_local_rotation = node.local_rotation;
    joint.rest_local_scale = node.local_scale;
    if (joint.parent_joint_index == world::kInvalidAnimationIndex) {
      skeleton.root_joint_indices.push_back(node_index);
    }
    skeleton.joints.push_back(std::move(joint));
  }
  return skeleton;
}

void populateGltfSkins(const GltfDocument& doc,
                       const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
                       GltfScenePrefab& prefab) {
  if (!doc.valid() || !doc.json.contains("skins") || !doc.json["skins"].is_array()) {
    return;
  }

  const auto gltf_node_to_prefab = buildGltfNodeToPrefabIndex(doc, node_indices_by_name);
  std::vector<uint32_t> gltf_skin_to_prefab_skin(doc.json["skins"].size(),
                                                 world::kInvalidAnimationIndex);
  uint32_t pose_skeleton_index = world::kInvalidAnimationIndex;

  for (size_t skin_index = 0; skin_index < doc.json["skins"].size(); ++skin_index) {
    const Json& source_skin = doc.json["skins"][skin_index];
    if (!source_skin.contains("joints") || !source_skin["joints"].is_array()) {
      continue;
    }

    world::Skin skin{};
    skin.name = source_skin.value("name", "Skin " + std::to_string(skin_index));

    for (const Json& joint_json : source_skin["joints"]) {
      if (!joint_json.is_number_unsigned()) {
        continue;
      }
      const uint32_t gltf_joint = joint_json.get<uint32_t>();
      const auto prefab_node_it = gltf_node_to_prefab.find(gltf_joint);
      if (prefab_node_it == gltf_node_to_prefab.end()) {
        continue;
      }
      skin.joint_node_indices.push_back(prefab_node_it->second);
    }

    if (skin.joint_node_indices.empty()) {
      continue;
    }

    if (source_skin.contains("inverseBindMatrices")) {
      std::vector<float> matrices;
      size_t matrix_count = 0;
      if (readFloatAccessor(doc,
                            source_skin["inverseBindMatrices"].get<uint32_t>(),
                            16,
                            matrices,
                            &matrix_count)) {
        for (size_t i = 0; i < matrix_count; ++i) {
          glm::mat4 matrix(1.0f);
          std::memcpy(&matrix[0][0], matrices.data() + i * 16, 16 * sizeof(float));
          skin.inverse_bind_matrices.push_back(matrix);
        }
      }
    }
    while (skin.inverse_bind_matrices.size() < skin.joint_node_indices.size()) {
      skin.inverse_bind_matrices.emplace_back(1.0f);
    }

    if (pose_skeleton_index == world::kInvalidAnimationIndex) {
      pose_skeleton_index = static_cast<uint32_t>(prefab.skeletons.size());
      prefab.skeletons.push_back(makeNodeHierarchySkeleton(
          prefab,
          skin.name.empty() ? std::string_view("Pose Skeleton")
                            : std::string_view(skin.name)));
    }

    skin.skeleton_index = pose_skeleton_index;
    const uint32_t prefab_skin_index = static_cast<uint32_t>(prefab.skins.size());
    prefab.skins.push_back(std::move(skin));
    gltf_skin_to_prefab_skin[skin_index] = prefab_skin_index;
  }

  if (!doc.json.contains("nodes") || !doc.json["nodes"].is_array()) {
    return;
  }
  for (size_t gltf_node_index = 0; gltf_node_index < doc.json["nodes"].size(); ++gltf_node_index) {
    const Json& node = doc.json["nodes"][gltf_node_index];
    if (!node.contains("skin")) {
      continue;
    }
    const uint32_t gltf_skin_index = node["skin"].get<uint32_t>();
    if (gltf_skin_index >= gltf_skin_to_prefab_skin.size() ||
        gltf_skin_to_prefab_skin[gltf_skin_index] == world::kInvalidAnimationIndex) {
      continue;
    }
    const uint32_t skin_index = gltf_skin_to_prefab_skin[gltf_skin_index];
    const auto prefab_node_it = gltf_node_to_prefab.find(static_cast<uint32_t>(gltf_node_index));
    if (prefab_node_it == gltf_node_to_prefab.end() ||
        prefab_node_it->second >= prefab.nodes.size()) {
      continue;
    }
    const world::Skin& skin = prefab.skins[skin_index];
    const Json* source_primitives = nullptr;
    if (node.contains("mesh") && node["mesh"].is_number_unsigned()) {
      const uint32_t mesh_index = node["mesh"].get<uint32_t>();
      if (doc.json.contains("meshes") &&
          doc.json["meshes"].is_array() &&
          mesh_index < doc.json["meshes"].size()) {
        const Json& mesh = doc.json["meshes"][mesh_index];
        if (mesh.contains("primitives") && mesh["primitives"].is_array()) {
          source_primitives = &mesh["primitives"];
        }
      }
    }

    auto& primitives = prefab.nodes[prefab_node_it->second].primitives;
    for (size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
      GltfScenePrefabPrimitive& primitive = primitives[primitive_index];
      primitive.skin_index = skin_index;
      primitive.joint_node_indices = skin.joint_node_indices;
      primitive.inverse_bind_matrices = skin.inverse_bind_matrices;
      if (source_primitives != nullptr && primitive_index < source_primitives->size()) {
        populatePrimitiveInfluencesFromGltf(doc,
                                            (*source_primitives)[primitive_index],
                                            skin,
                                            primitive);
      }
    }
  }
}

void populatePrimitiveSkinning(
    const aiScene& scene,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
    GltfScenePrefab& prefab) {
  uint32_t pose_skeleton_index = world::kInvalidAnimationIndex;
  for (const world::Skin& skin : prefab.skins) {
    if (skin.skeleton_index < prefab.skeletons.size()) {
      pose_skeleton_index = skin.skeleton_index;
      break;
    }
  }

  auto find_or_create_skin = [&](const std::string& base_name,
                                 const std::vector<uint32_t>& joint_node_indices,
                                 const std::vector<glm::mat4>& inverse_bind_matrices) {
    for (uint32_t skin_index = 0u; skin_index < prefab.skins.size(); ++skin_index) {
      const world::Skin& skin = prefab.skins[skin_index];
      if (skin.joint_node_indices == joint_node_indices &&
          skin.inverse_bind_matrices.size() == inverse_bind_matrices.size()) {
        return skin_index;
      }
    }

    if (pose_skeleton_index == world::kInvalidAnimationIndex) {
      pose_skeleton_index = static_cast<uint32_t>(prefab.skeletons.size());
      prefab.skeletons.push_back(makeNodeHierarchySkeleton(
          prefab,
          base_name.empty() ? std::string_view("Assimp Pose Skeleton")
                            : std::string_view(base_name)));
    }

    world::Skin skin{};
    skin.name = base_name.empty() ? "Assimp Skin" : base_name + " Skin";
    skin.skeleton_index = pose_skeleton_index;
    skin.joint_node_indices = joint_node_indices;
    skin.inverse_bind_matrices = inverse_bind_matrices;

    const uint32_t skin_index = static_cast<uint32_t>(prefab.skins.size());
    prefab.skins.push_back(std::move(skin));
    return skin_index;
  };

  for (GltfScenePrefabNode& node : prefab.nodes) {
    for (GltfScenePrefabPrimitive& primitive : node.primitives) {
      if (primitive.source_mesh_index >= scene.mNumMeshes ||
          scene.mMeshes[primitive.source_mesh_index] == nullptr) {
        continue;
      }
      const aiMesh& mesh = *scene.mMeshes[primitive.source_mesh_index];
      if (!mesh.HasBones() || primitive.mesh.vertices.empty()) {
        continue;
      }
      if (primitive.skin_index < prefab.skins.size() &&
          !primitive.joint_node_indices.empty() &&
          primitive.vertex_influences.size() == primitive.mesh.vertices.size()) {
        setInfluenceMeshAttributes(primitive);
        continue;
      }

      primitive.vertex_influences.assign(primitive.mesh.vertices.size(),
                                         components::VertexSkinInfluence{});
      primitive.joint_node_indices.clear();
      primitive.inverse_bind_matrices.clear();

      const world::Skin* explicit_skin = nullptr;
      if (primitive.skin_index < prefab.skins.size()) {
        explicit_skin = &prefab.skins[primitive.skin_index];
        primitive.joint_node_indices = explicit_skin->joint_node_indices;
        primitive.inverse_bind_matrices = explicit_skin->inverse_bind_matrices;
      } else {
        primitive.joint_node_indices.reserve(mesh.mNumBones);
        primitive.inverse_bind_matrices.reserve(mesh.mNumBones);
      }

      for (unsigned int bone_index = 0; bone_index < mesh.mNumBones; ++bone_index) {
        const aiBone* bone = mesh.mBones[bone_index];
        if (bone == nullptr) {
          continue;
        }
        const auto node_it = node_indices_by_name.find(bone->mName.C_Str());
        if (node_it == node_indices_by_name.end()) {
          continue;
        }

        uint32_t joint_index = world::kInvalidAnimationIndex;
        if (explicit_skin != nullptr) {
          const auto joint_it = std::find(explicit_skin->joint_node_indices.begin(),
                                          explicit_skin->joint_node_indices.end(),
                                          node_it->second);
          if (joint_it == explicit_skin->joint_node_indices.end()) {
            continue;
          }
          joint_index = static_cast<uint32_t>(
              std::distance(explicit_skin->joint_node_indices.begin(), joint_it));
        } else {
          joint_index = static_cast<uint32_t>(primitive.joint_node_indices.size());
          primitive.joint_node_indices.push_back(node_it->second);
          primitive.inverse_bind_matrices.push_back(toGlm(bone->mOffsetMatrix));
        }

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
      setInfluenceMeshAttributes(primitive);
      if (primitive.joint_node_indices.empty()) {
        primitive.vertex_influences.clear();
        primitive.inverse_bind_matrices.clear();
        primitive.mesh.joint_indices.clear();
        primitive.mesh.joint_weights.clear();
      } else if (explicit_skin == nullptr) {
        primitive.skin_index = find_or_create_skin(primitive.name,
                                                   primitive.joint_node_indices,
                                                   primitive.inverse_bind_matrices);
      }
    }
  }
}

}  // namespace karma::world
